/* DEVWORLD game IDE — Monaco-based, multi-language, in-browser execution.
   Exposes window.GameIDE for the C++ game shell to drive later. */

let editor = null;
let registry = [];
let current = null;
let pyodide = null, sqlDb = null;
const runCallbacks = [];

const $ = (id) => document.getElementById(id);
const outEl = () => $('output');

function print(text, cls) {
  const el = outEl();
  if (el.textContent === 'Pick a language and hit Run.') el.textContent = '';
  const span = document.createElement('span');
  if (cls) span.className = cls;
  span.textContent = text + '\n';
  el.appendChild(span);
  el.scrollTop = el.scrollHeight;
}
function clearOut() { outEl().textContent = ''; }
function setStatus(t) { $('status').textContent = t || ''; }

const SAMPLES = {
  python: 'print("Hello, DevWorld!")\nfor i in range(3):\n    print(f"mission {i+1} ready")',
  javascript: 'console.log("Hello, DevWorld!");\nfor (let i = 1; i <= 3; i++) {\n  console.log(`mission ${i} ready`);\n}',
  typescript: 'const greet = (name: string): string => `Hello, ${name}!`;\nconsole.log(greet("DevWorld"));',
  sql: 'CREATE TABLE missions (id INTEGER, title TEXT);\nINSERT INTO missions VALUES (1, \'Fix the bug\'), (2, \'Ship it\');\nSELECT * FROM missions;',
  lua: 'print("Hello, DevWorld!")\nfor i = 1, 3 do\n  print("mission " .. i .. " ready")\nend',
  c: '#include <stdio.h>\nint main() {\n    printf("Hello, DevWorld!\\n");\n    return 0;\n}',
  cpp: '#include <iostream>\nint main() {\n    std::cout << "Hello, DevWorld!" << std::endl;\n}',
  java: 'public class Main {\n    public static void main(String[] a) {\n        System.out.println("Hello, DevWorld!");\n    }\n}',
  go: 'package main\nimport "fmt"\nfunc main() {\n    fmt.Println("Hello, DevWorld!")\n}',
  rust: 'fn main() {\n    println!("Hello, DevWorld!");\n}',
  zig: 'const std = @import("std");\n pub fn main() void {\n    std.debug.print("Hello, DevWorld!\\n", .{});\n}',
};

function sampleFor(lang) {
  return SAMPLES[lang.id] || `// ${lang.name} — write code here`;
}

function monacoIdFor(lang) {
  if (!lang.monaco || typeof monaco === 'undefined') return 'plaintext';
  const ok = monaco.languages.getLanguages().some((l) => l.id === lang.monaco);
  return ok ? lang.monaco : 'plaintext';
}

const TIER_LABEL = { wasm: 'RUNS NOW', planned: 'SOON', server: 'SANDBOX LATER', highlight: 'EDITOR ONLY' };

function populateSelect() {
  const sel = $('langSelect');
  registry.forEach((l) => {
    const o = document.createElement('option');
    o.value = l.id;
    o.textContent = l.name;
    sel.appendChild(o);
  });
  sel.value = 'python';
  sel.addEventListener('change', () => setLanguage(sel.value));
}

function setLanguage(id) {
  const lang = registry.find((l) => l.id === id) || registry[0];
  current = lang;
  $('langSelect').value = lang.id;
  const badge = $('runBadge');
  badge.textContent = TIER_LABEL[lang.run] || '';
  badge.className = 'badge ' + lang.run;
  if (editor) {
    monaco.editor.setModelLanguage(editor.getModel(), monacoIdFor(lang));
    editor.setValue(sampleFor(lang));
  }
  clearOut();
  if (lang.run !== 'wasm') {
    print(`// ${lang.name}: ${tierNote(lang)}`);
  }
}

function tierNote(lang) {
  return {
    planned: 'in-browser runner coming soon — highlighting works today.',
    server: 'runs on the game sandbox (later) — highlighting works today.',
    highlight: 'editor support only.',
  }[lang.run] || '';
}

/* ---------------- runners ---------------- */

async function ensurePyodide() {
  if (!pyodide) {
    setStatus('loading Python runtime…');
    pyodide = await loadPyodide();
    setStatus('');
  }
  return pyodide;
}

async function runPython(code) {
  const py = await ensurePyodide();
  py.setStdout({ batched: (s) => print(s) });
  py.setStderr({ batched: (s) => print(s, 'err') });
  await py.runPythonAsync(code);
}

function runJavaScript(code) {
  const logs = [];
  const fakeConsole = { log: (...a) => logs.push(a.map(String).join(' ')) };
  const fn = new Function('console', `"use strict";\n${code}`);
  fn(fakeConsole);
  logs.forEach((l) => print(l));
  if (!logs.length) print('(no output)', 'ok');
}

async function runTypeScript(code) {
  if (typeof ts === 'undefined') throw new Error('TypeScript compiler failed to load.');
  const out = ts.transpileModule(code, {
    compilerOptions: { module: ts.ModuleKind.None, target: ts.ScriptTarget.ES2020 },
    reportDiagnostics: true,
  });
  (out.diagnostics || []).forEach((d) =>
    print('TS' + d.code + ': ' + ts.flattenDiagnosticMessageText(d.messageText, ' '), 'err'));
  runJavaScript(out.outputText);
}

async function ensureSqlJs() {
  if (!sqlDb) {
    setStatus('loading SQL engine…');
    const SQL = await initSqlJs({
      locateFile: (f) => `https://cdn.jsdelivr.net/npm/sql.js@1.15.0/dist/${f}`,
    });
    sqlDb = new SQL.Database();
    setStatus('');
  }
  return sqlDb;
}

async function runSql(code) {
  const db = await ensureSqlJs();
  const results = db.exec(code);
  if (!results.length) { print('(no result sets)', 'ok'); return; }
  results.forEach((r) => {
    print(r.columns.join(' | '), 'ok');
    print('-'.repeat(r.columns.join(' | ').length));
    r.values.forEach((row) => print(row.map((v) => String(v)).join(' | ')));
    print('');
  });
}

function runLua(code) {
  const { lua, lauxlib, lualib, to_luastring } = fengari;
  const L = lauxlib.luaL_newstate();
  lualib.luaL_openlibs(L);
  const prefix =
    '__out={}\nprint=function(...) local t={} ' +
    'for i=1,select("#",...) do t[i]=tostring(select(i,...)) end ' +
    '__out[#__out+1]=table.concat(t,"\\t") end\n';
  const st = lauxlib.luaL_dostring(L, to_luastring(prefix + code));
  if (st !== lua.LUA_OK) {
    print(lua.lua_tojsstring(L, -1), 'err');
    return;
  }
  lua.lua_getglobal(L, '__out');
  const n = lua.lua_rawlen(L, -1);
  for (let i = 1; i <= n; i++) {
    lua.lua_rawgeti(L, -1, i);
    print(lua.lua_tojsstring(L, -1));
    lua.lua_pop(L, 1);
  }
  if (n === 0) print('(no output)', 'ok');
}

const RUNNERS = {
  python: runPython,
  javascript: runJavaScript,
  typescript: runTypeScript,
  sql: runSql,
  lua: runLua,
};

async function runCode() {
  if (!current) return;
  clearOut();
  if (current.run !== 'wasm' || !RUNNERS[current.runner]) {
    print(`// ${current.name}: ${tierNote(current)}`);
    return;
  }
  const btn = $('runBtn');
  btn.disabled = true;
  const t0 = performance.now();
  try {
    await RUNNERS[current.runner](editor.getValue());
    runCallbacks.forEach((cb) => { try { cb(current.id, true); } catch (e) {} });
  } catch (e) {
    print(String((e && e.message) || e), 'err');
    runCallbacks.forEach((cb) => { try { cb(current.id, false); } catch (e2) {} });
  } finally {
    print(`\n— done in ${Math.round(performance.now() - t0)}ms`, 'ok');
    btn.disabled = false;
  }
}

/* ---------------- boot ---------------- */

function createEditor() {
  editor = monaco.editor.create($('editor'), {
    value: '',
    theme: 'vs-dark',
    fontSize: 14,
    minimap: { enabled: false },
    automaticLayout: true,
    scrollBeyondLastLine: false,
  });
  setLanguage('python');
  $('runBtn').addEventListener('click', runCode);
  window.GameIDE = {
    setLanguage,
    getCode: () => editor.getValue(),
    setCode: (c) => editor.setValue(c),
    runCode,
    onRun: (cb) => runCallbacks.push(cb),
    getLanguages: () => registry.map((l) => ({ id: l.id, name: l.name, run: l.run })),
  };
}

(async function boot() {
  try {
    registry = await (await fetch('languages.json')).json();
  } catch (e) {
    outEl().textContent = 'Failed to load languages.json: ' + e;
    return;
  }
  populateSelect();
  require(['vs/editor/editor.main'], createEditor);
})();
