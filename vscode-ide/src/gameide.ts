// window.GameIDE — the game shell contract. Same surface as the classic IDE,
// now backed by the real VS Code workbench.

import * as vscode from 'vscode'
import { vfs } from './vfs'
import { RUNNERS, CAPTURE, type RunHelpers } from './runners'
import {
  BACKEND_RUNTIME, getBackend, setBackend as saveBackend,
  runViaBackend, assessViaBackend, type TestCase, type TestResult
} from './backend'
import languagesJson from './languages.json'

export interface LangDef {
  id: string; name: string; ext: string; monaco: string
  run: 'wasm' | 'server' | 'planned' | 'highlight'; runner?: string
}
const LANGUAGES = languagesJson as LangDef[]

const TESTS_KEY = 'devworld.tests.v1'

type RunCallback = (langId: string, ok: boolean) => void
type TestCallback = (path: string, summary: { passed: number; total: number }) => void
const runCallbacks: RunCallback[] = []
const testCallbacks: TestCallback[] = []

let output: vscode.OutputChannel | null = null
function out(): vscode.OutputChannel {
  if (!output) output = vscode.window.createOutputChannel('DEVWORLD')
  return output
}

function helpers(): RunHelpers {
  const ch = out()
  return {
    print: (t: string, cls?: 'err' | 'ok') => ch.appendLine(t),
    setStatus: (_t: string) => {}
  }
}

export function langForPath(path: string): LangDef {
  const ext = path.split('/').pop()!.split('.').pop()!.toLowerCase()
  return LANGUAGES.find((l) => l.ext === ext) || LANGUAGES[0]
}
export function langById(id: string): LangDef {
  return LANGUAGES.find((l) => l.id === id) || LANGUAGES[0]
}

function tierNote(lang: LangDef): string {
  if (lang.run === 'server') {
    if (!BACKEND_RUNTIME[lang.id]) return 'no sandbox runtime for this language yet — highlighting works today.'
    return getBackend()
      ? 'runs in the game sandbox via your configured backend.'
      : 'needs a sandbox backend — run the "DEVWORLD: Set Sandbox Backend" command.'
  }
  return {
    planned: 'in-browser runner coming soon — highlighting works today.',
    highlight: 'editor support only.'
  }[lang.run] || ''
}

export async function runActiveFile(targetPath?: string): Promise<boolean> {
  let uri: vscode.Uri | undefined
  if (targetPath) {
    uri = vfs.uri(targetPath)
  } else {
    uri = vscode.window.activeTextEditor?.document.uri
  }
  if (!uri) {
    vscode.window.showInformationMessage('Open a file first.')
    return false
  }
  const path = vfs.pathFromUri(uri)
  const doc = await vscode.workspace.openTextDocument(uri)
  const code = doc.getText()
  const lang = langForPath(path)
  const ch = out()
  ch.show(true)

  if (lang.run === 'server' && BACKEND_RUNTIME[lang.id]) {
    ch.appendLine(`— running ${path} on the ${BACKEND_RUNTIME[lang.id]} sandbox…`)
    const ok = await runViaBackend(path, lang.id, helpers())
    runCallbacks.forEach((cb) => { try { cb(lang.id, ok) } catch { /* ignore */ } })
    return ok
  }
  const runner = lang.runner ? RUNNERS[lang.runner] : undefined
  if (lang.run !== 'wasm' || !runner) {
    ch.appendLine(`// ${lang.name}: ${tierNote(lang)}`)
    return false
  }
  const t0 = performance.now()
  try {
    await runner(code, helpers())
    ch.appendLine(`\n— done in ${Math.round(performance.now() - t0)}ms`)
    runCallbacks.forEach((cb) => { try { cb(lang.id, true) } catch { /* ignore */ } })
    return true
  } catch (e: any) {
    ch.appendLine('Error: ' + ((e && e.message) || e))
    runCallbacks.forEach((cb) => { try { cb(lang.id, false) } catch { /* ignore */ } })
    return false
  }
}

/* ---------------- test cases store ---------------- */
function loadTests(): Record<string, TestCase[]> {
  try {
    return JSON.parse(localStorage.getItem(TESTS_KEY) || '{}') || {}
  } catch {
    return {}
  }
}
function saveTests(all: Record<string, TestCase[]>): void {
  try {
    localStorage.setItem(TESTS_KEY, JSON.stringify(all))
  } catch { /* ignore */ }
}
export function getTestCases(path: string): TestCase[] {
  return loadTests()[path] || []
}
export function setTestCases(path: string, cases: TestCase[]): void {
  const all = loadTests()
  all[path] = cases
  saveTests(all)
}

function firstDiff(a: string[], b: string[]): number {
  const n = Math.max(a.length, b.length)
  for (let i = 0; i < n; i++) if ((a[i] || '') !== (b[i] || '')) return i
  return -1
}

export async function runTestsFor(
  path: string, code: string, cases: TestCase[]
): Promise<TestResult[]> {
  const lang = langForPath(path)
  if (lang.run === 'server' && BACKEND_RUNTIME[lang.id] && getBackend()) {
    return assessViaBackend(path, code, lang.id, cases)
  }
  const results: TestResult[] = []
  for (const tc of cases) {
    let actual = ''
    let crashed = false
    const cap = lang.runner ? CAPTURE[lang.runner] : undefined
    if (lang.run !== 'wasm' || !cap) {
      actual = `Error: ${lang.name} has no in-browser runner yet.\n`
      crashed = true
    } else {
      try {
        actual = await cap(code, tc.stdin || '')
      } catch (e: any) {
        actual = 'Error: ' + ((e && e.message) || e) + '\n'
        crashed = true
      }
    }
    results.push({
      name: tc.name,
      pass: !crashed && actual.trim() === (tc.expected || '').trim(),
      hidden: !!tc.hidden,
      actual, expected: tc.expected || '', crashed, diagnostics: ''
    })
  }
  return results
}

export function formatReport(results: TestResult[]): string {
  const lines: string[] = []
  let passed = 0
  for (const r of results) {
    if (r.pass) passed++
    lines.push(`${r.pass ? 'PASS' : 'FAIL'} — ${r.name}${r.hidden ? ' (hidden)' : ''}`)
    if (!r.pass && !r.hidden) {
      const a = r.actual.trimEnd().split('\n')
      const e = r.expected.trimEnd().split('\n')
      const di = firstDiff(a, e)
      e.slice(0, 10).forEach((l, i) => lines.push(`  + expected ${l}${i === di ? '   ← first difference' : ''}`))
      a.slice(0, 10).forEach((l, i) => lines.push(`  - actual   ${l}${i === di ? '   ← first difference' : ''}`))
    }
  }
  lines.push(`${passed}/${results.length} passed`)
  return lines.join('\n')
}

export async function runAllTests(): Promise<{ passed: number; total: number }> {
  const uri = vscode.window.activeTextEditor?.document.uri
  if (!uri) {
    vscode.window.showInformationMessage('Open a file to test.')
    return { passed: 0, total: 0 }
  }
  const path = vfs.pathFromUri(uri)
  const cases = getTestCases(path)
  const ch = out()
  ch.show(true)
  if (!cases.length) {
    ch.appendLine('No test cases — add one in the DEVWORLD Tests view first.')
    return { passed: 0, total: 0 }
  }
  const doc = await vscode.workspace.openTextDocument(uri)
  ch.appendLine(`— running ${cases.length} test(s) for ${path}`)
  const results = await runTestsFor(path, doc.getText(), cases)
  ch.appendLine(formatReport(results))
  const passed = results.filter((r) => r.pass).length
  const summary = { passed, total: results.length }
  testCallbacks.forEach((cb) => { try { cb(path, summary) } catch { /* ignore */ } })
  return summary
}

/* ---------------- public API ---------------- */
export function installGameIDE(): void {
  const api = {
    setLanguage: async (id: string) => {
      const lang = langById(id)
      const ed = vscode.window.activeTextEditor
      if (ed) {
        await vscode.languages.setTextDocumentLanguage(ed.document, lang.monaco || 'plaintext')
      }
      try { localStorage.setItem('devworld.lang.current', id) } catch { /* ignore */ }
    },
    getCode: () => vscode.window.activeTextEditor?.document.getText() ?? '',
    setCode: async (code: string) => {
      const ed = vscode.window.activeTextEditor
      if (!ed) return
      const doc = ed.document
      const edit = new vscode.WorkspaceEdit()
      edit.replace(doc.uri, new vscode.Range(0, 0, doc.lineCount, 0), code)
      await vscode.workspace.applyEdit(edit)
    },
    runCode: () => runActiveFile(),
    onRun: (cb: RunCallback) => { runCallbacks.push(cb) },
    runTests: () => runAllTests(),
    onTestRun: (cb: TestCallback) => { testCallbacks.push(cb) },
    openFile: async (p: string) => {
      const doc = await vscode.workspace.openTextDocument(vfs.uri(p))
      // Ensure the language matches the file extension (auto-detect can fail
      // if the language extension hasn't activated yet).
      const ext = p.split('.').pop()?.toLowerCase()
      const langMap: Record<string, string> = {
        ts: 'typescript', tsx: 'typescriptreact', js: 'javascript', jsx: 'javascriptreact',
        py: 'python', json: 'json', html: 'html', css: 'css', md: 'markdown'
      }
      if (ext && langMap[ext] && doc.languageId !== langMap[ext]) {
        try { await vscode.languages.setTextDocumentLanguage(doc, langMap[ext]) } catch { /* ignore */ }
      }
      await vscode.window.showTextDocument(doc)
    },
    activeFile: () => {
      const u = vscode.window.activeTextEditor?.document.uri
      return u ? vfs.pathFromUri(u) : null
    },
    listFiles: () => vfs.list(),
    readFile: (p: string) => vfs.read(p),
    writeFile: (p: string, c: string) => { vfs.write(p, c) },
    setBackend: (cfg: { url: string; apiKey: string }) => { saveBackend(cfg); return true },
    getBackend: () => getBackend(),
    getLanguages: () => LANGUAGES.map((l) => ({ id: l.id, name: l.name, run: l.run })),
    version: '0.3.0',
    __exts: () =>
      vscode.extensions.all.map((e: any) => ({ id: e.id, active: e.isActive }))
  }
  ;(window as any).GameIDE = api
}
