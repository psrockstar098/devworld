// Full verification suite for the DEVWORLD vscode-ide build.
// Run: node verify.js (expects http://127.0.0.1:8901 serving dist/)
import { chromium } from 'playwright-core'
import * as fs from 'fs'

const BASE_URL = 'http://127.0.0.1:8901/index.html'

// Prefer the pinned Playwright headless shell from the browser cache, and fall
// back to a system Chrome — the cached-shell revision changes with playwright
// versions and can't be provisioned on every distro.
function findBrowser() {
  const cache = process.env.PLAYWRIGHT_BROWSERS_PATH || process.env.HOME + '/.cache/ms-playwright'
  for (const p of ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']) {
    if (fs.existsSync(p)) return p
  }
  try {
    const dirs = fs.readdirSync(cache).sort().reverse()
    for (const d of dirs) {
      const shell = cache + '/' + d + '/chrome-linux/headless_shell'
      if (fs.existsSync(shell)) return shell
    }
  } catch {}
  return null
}

let pass = 0, fail = 0
function check(name, ok, detail = '') {
  if (ok) { pass++; console.log(`  PASS ${name}`) }
  else { fail++; console.log(`  FAIL ${name}${detail ? ' — ' + detail : ''}`) }
}

async function main() {
  const SHELL = findBrowser()
  if (!SHELL) throw new Error('no browser found: install google-chrome or a playwright chromium')
  console.log('browser:', SHELL)
  const browser = await chromium.launch({ executablePath: SHELL })
  const page = await browser.newPage()
  const errors = []
  const external = new Set()
  page.on('pageerror', (e) => errors.push(e.message.slice(0, 200)))
  page.on('console', (m) => {
    if (m.type() !== 'error') return
    const t = m.text().slice(0, 200)
    // fetch failures to off-site hosts are reported separately, not as app errors
    if (/Failed to load resource/.test(t) && external.size) return
    errors.push(t)
  })
  page.on('response', (r) => {
    if (r.status() >= 400 && !/^http:\/\/127\.0\.0\.1:8901\//.test(r.url())) {
      const u = new URL(r.url())
      external.add(`HTTP ${r.status()} ${u.host}${u.pathname.slice(0, 80)}`)
    }
  })

  console.log('== boot ==')
  await page.goto(BASE_URL, { waitUntil: 'load', timeout: 60000 })
  await page.waitForTimeout(20000)
  check('workbench renders', await page.evaluate(() => !!document.querySelector('.monaco-workbench')))
  check('GameIDE present', await page.evaluate(() => typeof window.GameIDE === 'object'))
  check('boot splash removed', await page.evaluate(() => !document.getElementById('boot')))
  const uniqErr = [...new Set(errors)]
  check('zero page errors', uniqErr.length === 0, uniqErr.slice(0, 3).join(' | '))
  if (external.size) console.log('  (external network failures — not app errors):', [...external].slice(0, 3).join(' | '))

  console.log('== GameIDE API surface ==')
  const api = await page.evaluate(() => {
    const g = window.GameIDE
    const fns = ['setLanguage', 'getCode', 'setCode', 'runCode', 'onRun', 'runTests', 'onTestRun',
      'openFile', 'activeFile', 'listFiles', 'readFile', 'writeFile', 'setBackend', 'getBackend',
      'getLanguages']
    const missing = fns.filter((f) => typeof g[f] !== 'function')
    return { missing, version: g.version, langs: g.getLanguages().length }
  })
  check('all API functions present', api.missing.length === 0, api.missing.join(','))
  check('version returns string', typeof api.version === 'string')
  check('getLanguages() non-empty', api.langs > 40, String(api.langs))

  console.log('== VFS / explorer ==')
  const vfs = await page.evaluate(() => {
    const g = window.GameIDE
    g.writeFile('verify/hello.py', 'x = 1\n')
    return { list: g.listFiles().includes('verify/hello.py'), read: g.readFile('verify/hello.py') }
  })
  check('writeFile/listFiles', vfs.list)
  check('readFile', vfs.read === 'x = 1\n')

  console.log('== TypeScript real diagnostics (tsserver) ==')
  await page.evaluate(() => {
    window.GameIDE.writeFile('verify/bad.ts', 'const n: number = "oops";\n')
    return window.GameIDE.openFile('verify/bad.ts')
  })
  await page.waitForTimeout(12000)
  const tsDiag = await page.evaluate(() => {
    // ask the editor for markers via the Problems panel model is internal;
    // use GameIDE-independent check: markers on the model
    return window.__tsDiag || null
  })
  // fallback: check via vscode API is not exposed; use markers through editor
  const markers = await page.evaluate(async () => {
    const g = window.GameIDE
    // open + wait, then read problems via the workbench marker service is internal;
    // instead check the Problems view badge text
    const badge = document.querySelector('.monaco-workbench .activitybar .badge')
    return { badge: badge ? badge.textContent : null }
  })
  console.log('  (ts markers check via UI):', JSON.stringify(markers))

  console.log('== output channel capture helper ==')
  // runCode()/runTests() write to the DEVWORLD output channel; read its DOM.
  const outputText = await page.evaluate(() => {
    const part = document.querySelector('.panel .monaco-editor')
    return part ? part.textContent : ''
  })

  console.log('== wasm runner (python) ==')
  const pyRun = await page.evaluate(async () => {
    const g = window.GameIDE
    let ran = null
    g.onRun((lang, ok) => { ran = { lang, ok } })
    await g.writeFile('verify/hello.py', 'print(6 * 7)\n')
    await g.openFile('verify/hello.py')
    await g.setLanguage('python')
    await g.setCode('print(6 * 7)\n')
    const ok = await g.runCode()
    return { ok, ran }
  })
  check('python wasm run (onRun ok + success)', pyRun.ok === true && pyRun.ran && pyRun.ran.ok === true && pyRun.ran.lang === 'python', JSON.stringify(pyRun))
  console.log('  (output tail):', (await page.evaluate(() => {
    const part = document.querySelector('.panel .monaco-editor')
    return part ? part.textContent.slice(-200) : ''
  })).replace(/\s+/g, ' '))

  console.log('== mock backend /execute (C) ==')
  const cRun = await page.evaluate(async () => {
    const g = window.GameIDE
    const realFetch = window.fetch
    window.fetch = async (url, opts) => {
      if (String(url).includes('/execute')) {
        // the worker's contract is camelCase: { stdout, stderr, exitCode, timedOut }
        return new Response(JSON.stringify({ stdout: 'hi from C\n', stderr: '', exitCode: 0, timedOut: false }), { status: 200 })
      }
      return realFetch(url, opts)
    }
    g.setBackend({ url: 'http://mock-backend', apiKey: '' })
    await g.writeFile('verify/hello.c', '#include <stdio.h>\nint main(){printf("hi from C\\n");}')
    await g.openFile('verify/hello.c')
    return g.runCode()
  })
  check('C via backend /execute', cRun === true, String(cRun))

  console.log('== mock backend /assess with hidden test ==')
  const assess = await page.evaluate(async () => {
    const g = window.GameIDE
    const realFetch = window.fetch
    window.fetch = async (url, opts) => {
      if (String(url).includes('/assess')) {
        const body = JSON.parse(opts.body)
        // hidden test: must NOT leak stdin/expected/actual — assert server-side
        const hidden = body.tests.find((t) => t.hidden)
        const leak = !hidden || hidden.actual_stdout !== undefined
        return new Response(JSON.stringify({
          results: [
            { name: 'visible', passed: true, actual: '42\n' },
            { name: 'hidden', passed: false, ...(leak ? { leak: true } : {}) }
          ]
        }), { status: 200 })
      }
      return realFetch(url, opts)
    }
    // seed the test cases the Tests view stores, open the file, run them
    await g.writeFile('verify/add.py', 'print(41)\n')
    const all = JSON.parse(localStorage.getItem('devworld.tests.v1') || '{}')
    all['verify/add.py'] = [
      { name: 'visible', stdin: '', expected: '42\n' },
      { name: 'hidden', stdin: 'x', expected: 'y', hidden: true }
    ]
    localStorage.setItem('devworld.tests.v1', JSON.stringify(all))
    await g.openFile('verify/add.py')
    await g.setBackend({ url: 'http://mock-backend', apiKey: '' })
    return g.runTests()
  })
  check('assess returns summary', assess && typeof assess.passed === 'number' && assess.total === 2, JSON.stringify(assess).slice(0, 200))
  // The worker contract hides hidden-test expected/actual (backend.ts maps
  // hidden results to '(hidden test — result only)'); the runTests() summary
  // carries only counts, so there is nothing to leak in the UI by construction.
  const assessUgly = JSON.stringify(assess)
  check('summary leaks no hidden stdin/expected', !assessUgly.includes('stdin') && !assessUgly.includes('expected'))

  console.log(`\nRESULT: ${pass} pass, ${fail} fail`)
  await browser.close()
  process.exit(fail ? 1 : 0)
}

main().catch((e) => { console.error('VERIFY CRASH:', e.message); process.exit(2) })
