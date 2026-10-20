// DEVWORLD sandbox backend wiring — same contract as the classic IDE:
// POST {url}/execute {runtime, files:[{path,content}], stdin?, timeout?}
// POST {url}/assess   {runtime, files, tests:[{name,stdin,expected_stdout,hidden?}], timeout?}
// Hidden tests: the worker only returns pass/fail — we never print expected/actual.

import { vfs } from './vfs'
import type { RunHelpers } from './runners'

const BACKEND_KEY = 'devworld.backend.v1'

// frontend language id -> execution-worker runtime id
export const BACKEND_RUNTIME: Record<string, string> = {
  c: 'c', cpp: 'cpp', csharp: 'csharp', java: 'java', go: 'go', rust: 'rust',
  zig: 'zig', kotlin: 'kotlin', swift: 'swift', dart: 'dart', julia: 'julia',
  scala: 'scala', haskell: 'haskell'
}

export interface BackendCfg { url: string; apiKey: string }

export function getBackend(): BackendCfg | null {
  try {
    const b = JSON.parse(localStorage.getItem(BACKEND_KEY) || 'null')
    return b && b.url ? { url: b.url, apiKey: b.apiKey || '' } : null
  } catch {
    return null
  }
}

export function setBackend(cfg: { url: string; apiKey: string }): void {
  try {
    localStorage.setItem(BACKEND_KEY, JSON.stringify({ url: cfg.url || '', apiKey: cfg.apiKey || '' }))
  } catch { /* ignore */ }
}

function headers(b: BackendCfg): Record<string, string> {
  return Object.assign(
    { 'Content-Type': 'application/json' },
    b.apiKey ? { 'x-api-key': b.apiKey } : {}
  )
}

function workspaceFiles(firstPath: string, override?: string): Array<{ path: string; content: string }> {
  const files = vfs.list().map((p) => ({
    path: p,
    content: p === firstPath && override !== undefined ? override : (vfs.read(p) || '')
  }))
  files.sort((a, b) => (a.path === firstPath ? -1 : b.path === firstPath ? 1 : 0))
  return files
}

export async function runViaBackend(path: string, langId: string, h: RunHelpers): Promise<boolean> {
  const b = getBackend()
  if (!b) {
    h.print(`// ${langId}: needs a sandbox backend — open Settings and set the backend URL + API key.`)
    return false
  }
  const t0 = performance.now()
  try {
    const res = await fetch(b.url.replace(/\/$/, '') + '/execute', {
      method: 'POST',
      headers: headers(b),
      body: JSON.stringify({
        runtime: BACKEND_RUNTIME[langId],
        files: workspaceFiles(path),
        timeout: 30
      })
    })
    const data = await res.json().catch(() => ({}))
    if (!res.ok) {
      h.print(`backend error ${res.status}: ${data.error || data.message || 'request failed'}`, 'err')
      return false
    }
    if (data.stdout) h.print(data.stdout)
    if (data.stderr) h.print(data.stderr, 'err')
    if (data.timedOut) h.print('// timed out', 'err')
    if (data.truncated) h.print('// output was truncated', 'ok')
    if (Array.isArray(data.artifacts) && data.artifacts.length) {
      h.print(`// ${data.artifacts.length} artifact(s) saved from /work/output`, 'ok')
    }
    h.print(`\n— done in ${Math.round(performance.now() - t0)}ms`, 'ok')
    return data.exitCode === 0
  } catch (e: any) {
    h.print('backend unreachable: ' + ((e && e.message) || e), 'err')
    return false
  }
}

export interface TestCase { name: string; stdin: string; expected: string; hidden?: boolean }
export interface TestResult {
  name: string; pass: boolean; hidden: boolean
  actual: string; expected: string; crashed: boolean; diagnostics: string
}

export async function assessViaBackend(
  path: string, code: string, langId: string, cases: TestCase[]
): Promise<TestResult[]> {
  const b = getBackend()
  if (!b) {
    return cases.map((tc) => ({
      name: tc.name, pass: false, hidden: !!tc.hidden,
      actual: 'Error: no sandbox backend configured.\n', expected: tc.expected || '',
      crashed: true, diagnostics: ''
    }))
  }
  let data: any = null
  try {
    const res = await fetch(b.url.replace(/\/$/, '') + '/assess', {
      method: 'POST',
      headers: headers(b),
      body: JSON.stringify({
        runtime: BACKEND_RUNTIME[langId],
        files: workspaceFiles(path, code),
        tests: cases.map((tc) => ({
          name: tc.name,
          stdin: tc.stdin || '',
          expected_stdout: tc.expected || '',
          hidden: !!tc.hidden
        })),
        timeout: 30
      })
    })
    data = await res.json().catch(() => ({}))
    if (!res.ok) throw new Error('backend error ' + res.status)
  } catch (e: any) {
    return cases.map((tc) => ({
      name: tc.name, pass: false, hidden: !!tc.hidden,
      actual: 'Error: ' + ((e && e.message) || e) + '\n', expected: tc.expected || '',
      crashed: true, diagnostics: ''
    }))
  }
  const byName: Record<string, any> = {}
  ;(data.results || []).forEach((r: any) => { byName[r.name] = r })
  return cases.map((tc) => {
    const r = byName[tc.name] || {}
    const hidden = !!tc.hidden
    return {
      name: tc.name,
      pass: !!r.passed,
      hidden,
      actual: hidden ? '(hidden test — result only)' : (r.actual != null ? String(r.actual) : ''),
      expected: hidden ? '' : (tc.expected || ''),
      crashed: !!r.errorKind,
      diagnostics: hidden ? '' : (r.error || '')
    }
  })
}
