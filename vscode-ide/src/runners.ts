// DEVWORLD wasm runners — lazy-loaded, no CDN. Heavy runtimes live in public/
// (pyodide/, sqljs/, ruby/, php/) and load on first use, exactly like the
// classic IDE did from CDN, but from local paths.

export interface RunHelpers {
  print(text: string, cls?: 'err' | 'ok'): void
  setStatus(text: string): void
}

const BASE = import.meta.env.BASE_URL || './'
function asset(p: string): string {
  return new URL(BASE + p, document.baseURI).toString()
}

function loadScript(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
    if (document.querySelector(`script[data-dw="${CSS.escape(src)}"]`)) return resolve()
    const s = document.createElement('script')
    s.src = src
    s.dataset.dw = src
    s.onload = () => resolve()
    s.onerror = () => reject(new Error('failed to load ' + src))
    document.head.appendChild(s)
  })
}

const enc = new TextEncoder()
const dec = new TextDecoder()

/* ---------------- python (pyodide) ---------------- */
let pyodide: any = null
async function ensurePyodide(h: RunHelpers): Promise<any> {
  if (!pyodide) {
    h.setStatus('Loading Python runtime…')
    try {
      await loadScript(asset('pyodide/pyodide.js'))
      pyodide = await (window as any).loadPyodide({ indexURL: asset('pyodide/') })
    } finally {
      h.setStatus('')
    }
  }
  return pyodide
}

async function runPython(code: string, h: RunHelpers): Promise<void> {
  const py = await ensurePyodide(h)
  py.setStdout({ batched: (s: string) => h.print(s) })
  py.setStderr({ batched: (s: string) => h.print(s, 'err') })
  await py.runPythonAsync(code)
}

async function capturePython(code: string, stdin: string): Promise<string> {
  const py = await ensurePyodide({ print() {}, setStatus() {} })
  let out = ''
  const data = stdin || ''
  let pos = 0
  py.setStdout({ batched: (s: string) => { out += s + '\n' } })
  py.setStderr({ batched: (s: string) => { out += s + '\n' } })
  if (typeof py.setStdin === 'function') {
    py.setStdin({
      stdin: () => (pos < data.length ? data.charCodeAt(pos++) : null),
      isatty: false
    })
  }
  try {
    await py.runPythonAsync(code)
  } catch (e: any) {
    out += 'Error: ' + ((e && e.message) || e) + '\n'
  }
  return out
}

export async function installPythonPackage(name: string): Promise<string> {
  const py = await ensurePyodide({ print() {}, setStatus() {} })
  await py.loadPackage('micropip')
  const mp = py.pyimport('micropip')
  await mp.install(name)
  return `installed ${name}`
}

/* ---------------- javascript ---------------- */
function runJavaScript(code: string, h: RunHelpers): void {
  const logs: string[] = []
  const fakeConsole = { log: (...a: any[]) => logs.push(a.map(String).join(' ')) }
  const fn = new Function('console', '"use strict";\n' + code)
  fn(fakeConsole)
  logs.forEach((l) => h.print(l))
  if (!logs.length) h.print('(no output)', 'ok')
}

async function captureJavaScript(code: string, stdin: string): Promise<string> {
  const lines = (stdin || '').split('\n')
  let li = 0
  let out = ''
  const fakeConsole = {
    log: (...a: any[]) => { out += a.map(String).join(' ') + '\n' },
    error: (...a: any[]) => { out += a.map(String).join(' ') + '\n' },
    warn: (...a: any[]) => { out += a.map(String).join(' ') + '\n' }
  }
  const prompt = () => (li < lines.length ? lines[li++] : '')
  const AsyncFn = Object.getPrototypeOf(async function () {}).constructor
  try {
    const fn = new AsyncFn('console', 'prompt', 'readline', '"use strict";\n' + code)
    await fn(fakeConsole, prompt, prompt)
  } catch (e: any) {
    out += 'Error: ' + ((e && e.message) || e) + '\n'
  }
  return out
}

/* ---------------- typescript ---------------- */
let tsMod: any = null
async function ensureTs(): Promise<any> {
  if (!tsMod) tsMod = await import('typescript')
  return tsMod
}

async function runTypeScript(code: string, h: RunHelpers): Promise<void> {
  const ts = await ensureTs()
  const out = ts.transpileModule(code, {
    compilerOptions: { module: ts.ModuleKind.None, target: ts.ScriptTarget.ES2020 },
    reportDiagnostics: true
  })
  ;(out.diagnostics || []).forEach((d: any) =>
    h.print('TS' + d.code + ': ' + ts.flattenDiagnosticMessageText(d.messageText, ' '), 'err'))
  runJavaScript(out.outputText, h)
}

async function captureTypeScript(code: string, stdin: string): Promise<string> {
  const ts = await ensureTs()
  const out = ts.transpileModule(code, {
    compilerOptions: { module: ts.ModuleKind.None, target: ts.ScriptTarget.ES2020 }
  })
  return captureJavaScript(out.outputText, stdin)
}

/* ---------------- sql (sql.js) ---------------- */
let sqlCtor: any = null
async function ensureSqlJs(h: RunHelpers): Promise<any> {
  if (!sqlCtor) {
    h.setStatus('Loading SQL engine…')
    try {
      await loadScript(asset('sqljs/sql-wasm.js'))
      sqlCtor = await (window as any).initSqlJs({
        locateFile: (f: string) => asset('sqljs/' + f)
      })
    } finally {
      h.setStatus('')
    }
  }
  return sqlCtor
}

async function runSql(code: string, h: RunHelpers): Promise<void> {
  const SQL = await ensureSqlJs(h)
  const db = new SQL.Database()
  try {
    const results = db.exec(code)
    if (!results.length) { h.print('(no result sets)', 'ok'); return }
    results.forEach((r: any) => {
      h.print(r.columns.join(' | '), 'ok')
      h.print('-'.repeat(r.columns.join(' | ').length))
      r.values.forEach((row: any[]) => h.print(row.map((v) => String(v)).join(' | ')))
      h.print('')
    })
  } finally {
    try { db.close() } catch { /* ignore */ }
  }
}

async function captureSql(code: string): Promise<string> {
  const SQL = await ensureSqlJs({ print() {}, setStatus() {} })
  const db = new SQL.Database()
  let out = ''
  try {
    const results = db.exec(code)
    results.forEach((r: any) => {
      out += r.columns.join(' | ') + '\n'
      r.values.forEach((row: any[]) => { out += row.map((v) => String(v)).join(' | ') + '\n' })
    })
  } catch (e: any) {
    out += 'Error: ' + ((e && e.message) || e) + '\n'
  } finally {
    try { db.close() } catch { /* ignore */ }
  }
  return out
}

/* ---------------- lua (fengari) ---------------- */
let fengari: any = null
async function ensureFengari(): Promise<any> {
  if (!fengari) {
    const m: any = await import('fengari-web')
    fengari = m.default || m
  }
  return fengari
}

const LUA_PREFIX =
  '__out={}\nprint=function(...) local t={} ' +
  'for i=1,select("#",...) do t[i]=tostring(select(i,...)) end ' +
  '__out[#__out+1]=table.concat(t,"\\t") end\n'

async function runLua(code: string, h: RunHelpers): Promise<void> {
  const fg = await ensureFengari()
  const { lua, lauxlib, lualib, to_luastring } = fg
  const L = lauxlib.luaL_newstate()
  lualib.luaL_openlibs(L)
  const out: string[] = []
  const st = lauxlib.luaL_dostring(L, to_luastring(LUA_PREFIX + code))
  if (st !== lua.LUA_OK) {
    h.print(lua.lua_tojsstring(L, -1), 'err')
    return
  }
  lua.lua_getglobal(L, '__out')
  const n = lua.lua_rawlen(L, -1)
  for (let i = 1; i <= n; i++) {
    lua.lua_rawgeti(L, -1, i)
    out.push(lua.lua_tojsstring(L, -1))
    lua.lua_pop(L, 1)
  }
  if (!n) h.print('(no output)', 'ok')
  else out.forEach((l) => h.print(l))
}

async function captureLua(code: string, stdin: string): Promise<string> {
  const fg = await ensureFengari()
  const { lua, lauxlib, lualib, to_luastring } = fg
  const L = lauxlib.luaL_newstate()
  lualib.luaL_openlibs(L)
  lua.lua_pushstring(L, to_luastring(stdin || ''))
  lua.lua_setglobal(L, '__dw_stdin')
  const prefix =
    '__out={}\nprint=function(...) local t={} ' +
    'for i=1,select("#",...) do t[i]=tostring(select(i,...)) end ' +
    '__out[#__out+1]=table.concat(t,"\\t") end\n' +
    '__dw_lines={} for s in (__dw_stdin.."\\n"):gmatch("(.-)\\n") do ' +
    '__dw_lines[#__dw_lines+1]=s end\n__dw_i=0\n' +
    'io.read=function() __dw_i=__dw_i+1 return __dw_lines[__dw_i] end\n'
  let out = ''
  const st = lauxlib.luaL_dostring(L, to_luastring(prefix + code))
  if (st !== lua.LUA_OK) return 'Error: ' + lua.lua_tojsstring(L, -1) + '\n'
  lua.lua_getglobal(L, '__out')
  const n = lua.lua_rawlen(L, -1)
  for (let i = 1; i <= n; i++) {
    lua.lua_rawgeti(L, -1, i)
    out += lua.lua_tojsstring(L, -1) + '\n'
    lua.lua_pop(L, 1)
  }
  return out
}

/* ---------------- ruby (ruby.wasm) ---------------- */
let rubyRT: any = null
async function ensureRuby(h: RunHelpers): Promise<any> {
  if (rubyRT) return rubyRT
  h.setStatus('Loading Ruby runtime…')
  try {
    await loadScript(asset('ruby/browser.umd.js'))
    const DefaultRubyVM = (window as any)['ruby-wasm-wasi'].DefaultRubyVM
    const mod = await WebAssembly.compileStreaming(fetch(asset('ruby/ruby+stdlib.wasm')))
    const r = await DefaultRubyVM(mod, { consolePrint: false })
    await r.vm.evalAsync('$stdout.sync = true; $stderr.sync = true')
    rubyRT = { vm: r.vm, wasi: r.wasi }
    return rubyRT
  } finally {
    h.setStatus('')
  }
}

function rubyResetFds(rt: any, stdin: string): void {
  const inBytes = enc.encode(stdin || '')
  const f0 = rt.wasi.fds[0]
  f0.file.data = new Uint8Array(inBytes)
  f0.file_pos = 0n
  ;[1, 2].forEach((fd) => {
    rt.wasi.fds[fd].file.data = new Uint8Array([])
    rt.wasi.fds[fd].file_pos = 0n
  })
}

async function captureRuby(code: string, stdin: string): Promise<string> {
  const rt = await ensureRuby({ print() {}, setStatus() {} })
  rubyResetFds(rt, stdin)
  let thrown = ''
  try {
    await rt.vm.evalAsync(code)
  } catch (e: any) {
    thrown = 'Error: ' + ((e && e.message) || e) + '\n'
  }
  const out = dec.decode(rt.wasi.fds[1].file.data)
  const err = dec.decode(rt.wasi.fds[2].file.data)
  return (out ? out + (out.endsWith('\n') ? '' : '\n') : '') +
    (err ? err + (err.endsWith('\n') ? '' : '\n') : '') + thrown
}

async function runRuby(code: string, h: RunHelpers): Promise<void> {
  const out = await captureRuby(code, '')
  const t = out.trimEnd()
  if (t) h.print(t)
}

/* ---------------- php (php-wasm) ---------------- */
let phpRT: any = null
async function ensurePhp(h: RunHelpers): Promise<any> {
  if (phpRT) return phpRT
  h.setStatus('Loading PHP runtime…')
  try {
    const m: any = await import(/* @vite-ignore */ asset('php/PhpWeb.mjs'))
    const php = new m.PhpWeb()
    const rt = { php, outChunks: [] as string[], errChunks: [] as string[] }
    php.addEventListener('output', (e: any) => { rt.outChunks.push(e.detail[0]) })
    php.addEventListener('error', (e: any) => { rt.errChunks.push(e.detail[0]) })
    await new Promise<void>((resolve, reject) => {
      php.addEventListener('ready', () => resolve(), { once: true })
      setTimeout(() => reject(new Error('PHP runtime init timed out')), 120000)
    })
    phpRT = rt
    return phpRT
  } finally {
    h.setStatus('')
  }
}

async function capturePhp(code: string, stdin: string): Promise<string> {
  const rt = await ensurePhp({ print() {}, setStatus() {} })
  rt.outChunks.length = 0
  rt.errChunks.length = 0
  if (stdin) {
    const codes: number[] = []
    for (let i = 0; i < stdin.length; i++) codes.push(stdin.charCodeAt(i))
    rt.php.buffers.stdin.push.apply(rt.php.buffers.stdin, codes)
  }
  let thrown = ''
  try {
    await rt.php.run(code)
  } catch (e: any) {
    thrown = 'Error: ' + ((e && e.message) || e) + '\n'
  }
  return rt.outChunks.join('') + rt.errChunks.join('') + thrown
}

async function runPhp(code: string, h: RunHelpers): Promise<void> {
  const out = await capturePhp(code, '')
  const t = out.trimEnd()
  if (t) h.print(t)
}

/* ---------------- registry ---------------- */
export const RUNNERS: Record<string, (code: string, h: RunHelpers) => void | Promise<void>> = {
  python: runPython,
  javascript: (c, h) => runJavaScript(c, h),
  typescript: runTypeScript,
  sql: runSql,
  lua: runLua,
  ruby: runRuby,
  php: runPhp
}

export const CAPTURE: Record<string, (code: string, stdin: string) => Promise<string>> = {
  python: capturePython,
  javascript: captureJavaScript,
  typescript: captureTypeScript,
  sql: captureSql,
  lua: captureLua,
  ruby: captureRuby,
  php: capturePhp
}
