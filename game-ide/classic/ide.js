/* DEVWORLD game IDE — Monaco-based, multi-file, in-browser execution.
   Exposes window.GameIDE for the C++ game shell (mission verification etc).
   Modules (loaded before this file): explorer.js (VFS/Tabs/Search/Workspaces/
   StatusBar/SettingsUI), terminal.js (Term/showBottom), debugger.js (Debug),
   tests.js (TestsUI/captureRun), docs.js (DocsUI), templates.js (TemplatesUI),
   packages.js (PackagesUI), gitpanel.js (GitUI), runners-extra.js (ruby/php/r). */

let editor = null;
let registry = [];
let current = null;
let pyodide = null, sqlDb = null, sqlJsCtor = null;
let lastActive = null;
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
function setStatus(t) { const s = $('status'); if (s) s.textContent = t || ''; }

const SAMPLES = {
  python: 'print("Hello, DevWorld!")\nfor i in range(3):\n    print(f"mission {i+1} ready")',
  javascript: 'console.log("Hello, DevWorld!");\nfor (let i = 1; i <= 3; i++) {\n  console.log(`mission ${i} ready`);\n}',
  typescript: 'const greet = (name: string): string => `Hello, ${name}!`;\nconsole.log(greet("DevWorld"));',
  sql: 'CREATE TABLE missions (id INTEGER, title TEXT);\nINSERT INTO missions VALUES (1, \'Fix the bug\'), (2, \'Ship it\');\nSELECT * FROM missions;',
  lua: 'print("Hello, DevWorld!")\nfor i = 1, 3 do\n  print("mission " .. i .. " ready")\nend',
  ruby: 'puts "Hello, DevWorld!"\n3.times { |i| puts "mission #{i + 1} ready" }',
  php: '<?php\necho "Hello, DevWorld!\\n";\nfor ($i = 1; $i <= 3; $i++) {\n    echo "mission $i ready\\n";\n}\n',
  r: 'print("Hello, DevWorld!")\nfor (i in 1:3) {\n  print(paste("mission", i, "ready"))\n}',
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
  if (!lang || !lang.monaco || typeof monaco === 'undefined') return 'plaintext';
  const ok = monaco.languages.getLanguages().some((l) => l.id === lang.monaco);
  return ok ? lang.monaco : 'plaintext';
}

/* language resolution: file extension -> registry entry */
function langForPath(path) {
  if (!path) return current || registry[0];
  const ext = VFS.ext(path);
  const hits = registry.filter((l) => l.ext === ext);
  if (hits.length) return hits[0];
  const base = path.split('/').pop().toLowerCase();
  if (base === 'dockerfile') return registry.find((l) => l.id === 'docker') || current || registry[0];
  return current || registry[0];
}
function currentLang() { return current || registry[0]; }

const TIER_LABEL = { wasm: 'RUNS NOW', planned: 'SOON', server: 'SANDBOX LATER', highlight: 'EDITOR ONLY' };

// frontend language id -> execution-worker runtime id (sandboxed, Docker-isolated)
const BACKEND_RUNTIME = {
  c: 'c', cpp: 'cpp', csharp: 'csharp', java: 'java', go: 'go', rust: 'rust',
  zig: 'zig', kotlin: 'kotlin', swift: 'swift', dart: 'dart', julia: 'julia',
  scala: 'scala', haskell: 'haskell',
};

function backendCfg() {
  try {
    const b = JSON.parse(localStorage.getItem('devworld.backend.v1') || 'null');
    return b && b.url ? b : null;
  } catch (e) { return null; }
}

function backendHeaders(b) {
  return Object.assign({ 'Content-Type': 'application/json' },
    b.apiKey ? { 'x-api-key': b.apiKey } : {});
}

// Approved-dependency selections per backend runtime and the tracked submission
// id live in classic/deps-store.js (window.DWDeps), so both the IDE and its
// Node unit tests share one implementation.

function workspaceFiles(firstPath, overrideContent) {
  const files = VFS.list().map((p) => ({
    path: p,
    content: (p === firstPath && overrideContent !== undefined) ? overrideContent : (VFS.read(p) || ''),
  }));
  files.sort((a, c) => (a.path === firstPath ? -1 : c.path === firstPath ? 1 : 0));
  return files;
}

function populateSelect() {
  const sel = $('langSelect');
  registry.forEach((l) => {
    const o = document.createElement('option');
    o.value = l.id;
    o.textContent = l.name;
    sel.appendChild(o);
  });
  sel.addEventListener('change', () => setLanguage(sel.value));
}

function updateBadge() {
  const lang = currentLang();
  const badge = $('runBadge');
  let label = TIER_LABEL[lang.run] || '';
  let cls = lang.run;
  if (lang.run === 'server' && BACKEND_RUNTIME[lang.id] && backendCfg()) {
    label = 'SANDBOX READY';
    cls = 'ready';
  }
  badge.textContent = label;
  badge.className = 'badge ' + cls;
}

function setLanguage(id) {
  const lang = registry.find((l) => l.id === id) || registry[0];
  current = lang;
  $('langSelect').value = lang.id;
  updateBadge();
  const path = Tabs.active();
  if (path && lang.ext) {
    // keep file extension in sync with the chosen language
    const curExt = VFS.ext(path);
    if (curExt !== lang.ext) {
      const dir = path.includes('/') ? path.slice(0, path.lastIndexOf('/') + 1) : '';
      const base = path.split('/').pop().replace(/\.[^.]*$/, '');
      const np = dir + base + '.' + lang.ext;
      if (!VFS.exists(np)) {
        VFS.rename(path, np);
        Tabs.onRename(path, np);
      }
    }
  }
  if (editor) {
    monaco.editor.setModelLanguage(editor.getModel(), monacoIdFor(currentLang()));
  }
  if (path) {
    StatusBar.lang(currentLang().name);
    Explorer.render();
  }
  clearOut();
  if (lang.run !== 'wasm') {
    print(`// ${lang.name}: ${tierNote(lang)}`);
  }
}

function tierNote(lang) {
  if (lang.run === 'server') {
    if (!BACKEND_RUNTIME[lang.id])
      return 'no sandbox runtime for this language yet — highlighting works today.';
    return backendCfg()
      ? 'runs in the game sandbox via your configured backend.'
      : 'needs a sandbox backend — open Settings and set the backend URL + API key.';
  }
  return {
    planned: 'in-browser runner coming soon — highlighting works today.',
    highlight: 'editor support only.',
  }[lang.run] || '';
}

/* ---------------- runners (kept from v1) ---------------- */

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

async function ensureSqlJsCtor() {
  if (!sqlJsCtor) {
    setStatus('loading SQL engine…');
    sqlJsCtor = await initSqlJs({
      locateFile: (f) => `https://cdn.jsdelivr.net/npm/sql.js@1.15.0/dist/${f}`,
    });
    setStatus('');
  }
  return sqlJsCtor;
}

async function ensureSqlJs() {
  const SQL = await ensureSqlJsCtor();
  if (!sqlDb) sqlDb = new SQL.Database();
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
// ruby / php / r register here from runners-extra.js (loaded before this file)
Object.assign(RUNNERS, window.EXTRA_RUNNERS || {});

/* ---------------- running files ---------------- */

function saveActiveFile() {
  const p = Tabs.active();
  if (!p || !editor) return;
  const v = editor.getValue();
  if (VFS.read(p) !== v) VFS.write(p, v);
  Tabs.refreshDirty();
  Explorer.render();
}

async function runViaBackend(path, lang) {
  const b = backendCfg();
  if (!b) {
    print(`// ${lang.name}: needs a sandbox backend — open Settings and set the backend URL + API key.`);
    return;
  }
  const btn = $('runBtn');
  btn.disabled = true;
  const t0 = performance.now();
  try {
    const res = await fetch(b.url.replace(/\/$/, '') + '/execute', {
      method: 'POST',
      headers: backendHeaders(b),
      body: JSON.stringify({
        runtime: BACKEND_RUNTIME[lang.id],
        files: workspaceFiles(path),
        timeout: 30,
        dependencies: DWDeps.get(BACKEND_RUNTIME[lang.id]),
      }),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      print(`backend error ${res.status}: ${data.error || data.message || 'request failed'}`, 'err');
      return;
    }
    if (data.stdout) print(data.stdout);
    if (data.stderr) print(data.stderr, 'err');
    if (data.timedOut) print('// timed out after ' + (data.timeoutMs || '?') + 'ms', 'err');
    if (data.truncated) print('// output was truncated', 'ok');
    if (Array.isArray(data.artifacts) && data.artifacts.length)
      print(`// ${data.artifacts.length} artifact(s) saved from /work/output`, 'ok');
    runCallbacks.forEach((cb) => { try { cb(lang.id, data.exitCode === 0); } catch (e) {} });
  } catch (e) {
    print('backend unreachable: ' + ((e && e.message) || e), 'err');
  } finally {
    print(`\n— done in ${Math.round(performance.now() - t0)}ms`, 'ok');
    btn.disabled = false;
  }
}

async function runFile(path) {
  path = path || Tabs.active();
  if (!path) { print('No file open.'); return; }
  saveActiveFile();
  showBottom('output');
  const code = VFS.read(path) || '';
  const lang = langForPath(path);
  clearOut();
  if (lang.run === 'server' && BACKEND_RUNTIME[lang.id]) {
    await runViaBackend(path, lang);
    return;
  }
  const runner = RUNNERS[lang.runner];
  if (lang.run !== 'wasm' || !runner) {
    print(`// ${lang.name}: ${tierNote(lang)}`);
    return;
  }
  const btn = $('runBtn');
  btn.disabled = true;
  const t0 = performance.now();
  try {
    await runner(code, { print, setStatus, clearOut });
    runCallbacks.forEach((cb) => { try { cb(lang.id, true); } catch (e) {} });
  } catch (e) {
    print(String((e && e.message) || e), 'err');
    runCallbacks.forEach((cb) => { try { cb(lang.id, false); } catch (e2) {} });
  } finally {
    print(`\n— done in ${Math.round(performance.now() - t0)}ms`, 'ok');
    btn.disabled = false;
  }
}
// backwards-compatible alias
function runCode() { return runFile(); }

function startDebug() {
  const path = Tabs.active();
  if (!path) { print('Open a file to debug.'); return; }
  const lang = langForPath(path);
  if (lang.id !== 'javascript') {
    showBottom('output');
    print(`Debugger currently supports JavaScript only (open file is ${lang.name}).`, 'err');
    return;
  }
  saveActiveFile();
  Debug.start(path, editor.getValue());
}

/* ---------------- editor <-> tabs ---------------- */

function openPath(path) {
  if (!path || !VFS.exists(path)) {
    if (editor) editor.setValue('');
    return;
  }
  const lang = langForPath(path);
  current = lang;
  $('langSelect').value = lang.id;
  updateBadge();
  editor.setValue(VFS.read(path) || '');
  monaco.editor.setModelLanguage(editor.getModel(), monacoIdFor(lang));
  StatusBar.lang(lang.name);
  Explorer.render();
  Tabs.render();
}

function refreshEditorForPath() {
  const p = Tabs.active();
  if (p && editor && VFS.exists(p)) {
    const v = VFS.read(p) || '';
    if (editor.getValue() !== v) {
      editor.setValue(v);
      monaco.editor.setModelLanguage(editor.getModel(), monacoIdFor(langForPath(p)));
    }
  }
  lastActive = p;
  Explorer.render();
  Tabs.render();
  Debug.refreshDecorations();
}

function revealLine(n) {
  if (!editor) return;
  editor.revealLineInCenter(n);
  editor.setPosition({ lineNumber: n, column: 1 });
  editor.focus();
}

function renderProblems() {
  if (!editor || typeof monaco === 'undefined') return;
  const markers = monaco.editor.getModelMarkers({ resource: editor.getModel().uri });
  const host = $('problems');
  host.innerHTML = '';
  let bad = 0;
  markers.slice(0, 200).forEach((m) => {
    const sev = m.severity === monaco.MarkerSeverity.Error ? 'err'
      : m.severity === monaco.MarkerSeverity.Warning ? 'warn' : 'info';
    if (sev !== 'info') bad++;
    const div = document.createElement('div');
    div.className = 'prob ' + sev;
    div.textContent = `Line ${m.startLineNumber}: ${m.message}`;
    div.addEventListener('click', () => revealLine(m.startLineNumber));
    host.appendChild(div);
  });
  if (!markers.length) host.innerHTML = '<p class="subtle" style="padding:6px">No problems.</p>';
  $('probCount').textContent = bad ? `(${bad})` : '';
  StatusBar.problems(bad);
}

/* ---------------- UI wiring ---------------- */

function wireUI() {
  // tab switches: stash previous file's editor content first
  Tabs.onSwitch.push((path) => {
    if (lastActive && editor && VFS.exists(lastActive)) {
      const v = editor.getValue();
      if (VFS.read(lastActive) !== v) VFS.write(lastActive, v);
    }
    lastActive = path;
    openPath(path);
    Debug.refreshDecorations();
    TestsUI.render();
  });

  // activity bar
  document.querySelectorAll('.abtn').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('.abtn').forEach((x) => x.classList.remove('active'));
      b.classList.add('active');
      const v = b.dataset.view;
      document.querySelectorAll('.sview').forEach((s) =>
        s.classList.toggle('active', s.id === 'view-' + v));
      if (v === 'git') GitUI.ensure();
      else if (v === 'tests') TestsUI.render();
      else if (v === 'docs') DocsUI.render($('docsInput').value);
      else if (v === 'packages') PackagesUI.render();
      else if (v === 'search') setTimeout(() => $('searchInput').focus(), 30);
    });
  });

  // bottom panel tabs
  document.querySelectorAll('.bptab').forEach((b) => {
    b.addEventListener('click', () => showBottom(b.dataset.bp));
  });

  // header
  $('runBtn').addEventListener('click', () => runFile());
  $('testBtn').addEventListener('click', () => TestsUI.runAll());
  $('debugBtn').addEventListener('click', startDebug);
  $('stepBtn').addEventListener('click', () => Debug.stepOver());
  $('contBtn').addEventListener('click', () => Debug.continue_());
  $('stopBtn').addEventListener('click', () => Debug.stop());

  // explorer actions
  $('newFileBtn').addEventListener('click', () => Explorer.newFile());
  $('newFolderBtn').addEventListener('click', () => Explorer.newFolder());
  $('newProjectBtn').addEventListener('click', () => TemplatesUI.dialog());

  // search
  $('searchInput').addEventListener('input', (e) => Search.run(e.target.value));
  // docs filter
  $('docsInput').addEventListener('input', (e) => DocsUI.render(e.target.value));
  // tests
  $('addTestBtn').addEventListener('click', () => TestsUI.add());
  $('runTestsBtn').addEventListener('click', () => TestsUI.runAll());

  // ctrl/cmd+s
  window.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
      e.preventDefault();
      saveActiveFile();
      setStatus('saved');
      setTimeout(() => setStatus(''), 1200);
    }
  });

  // modal dismiss on backdrop click
  $('modal').addEventListener('click', (e) => {
    if (e.target.id === 'modal') e.target.classList.add('hidden');
  });
}

/* ---------------- boot ---------------- */

function createEditor() {
  let fontSize = 14;
  try { fontSize = parseInt(localStorage.getItem('devworld.fontsize') || '14', 10) || 14; } catch (e) {}
  editor = monaco.editor.create($('editor'), {
    value: '',
    theme: 'vs-dark',
    fontSize,
    minimap: { enabled: false },
    automaticLayout: true,
    scrollBeyondLastLine: false,
    glyphMargin: true,
  });
  editor.onDidChangeModelContent(() => Tabs.refreshDirty());
  monaco.editor.onDidChangeMarkers(() => renderProblems());
  Debug.attach(editor);
  wireUI();

  window.GameIDE = {
    setLanguage,
    getCode: () => editor.getValue(),
    setCode: (c) => editor.setValue(c),
    runCode: () => runFile(),
    onRun: (cb) => runCallbacks.push(cb),
    getLanguages: () => registry.map((l) => ({ id: l.id, name: l.name, run: l.run })),
    openFile: (p) => Tabs.openFile(p),
    activeFile: () => Tabs.active(),
    listFiles: () => VFS.list(),
    readFile: (p) => VFS.read(p),
    writeFile: (p, c) => {
      VFS.write(p, c);
      if (p === Tabs.active()) refreshEditorForPath();
      Explorer.render();
    },
    runTests: () => TestsUI.runAll(),
    onTestRun: (cb) => testCallbacks.push(cb),
    setBackend: (cfg) => {
      try {
        localStorage.setItem('devworld.backend.v1',
          JSON.stringify({ url: (cfg && cfg.url) || '', apiKey: (cfg && cfg.apiKey) || '' }));
      } catch (e) {}
      return true;
    },
    getBackend: () => {
      try { return JSON.parse(localStorage.getItem('devworld.backend.v1') || 'null'); }
      catch (e) { return null; }
    },

    // --- backend project history (execution-worker submissions/revisions) ---
    backendDependencies: (runtime) => DWDeps.get(runtime),
    setBackendDependency: (runtime, name, on) => {
      const list = DWDeps.get(runtime).filter((n) => n !== name);
      if (on) list.push(name);
      DWDeps.set(runtime, list);
      if (typeof PackagesUI !== 'undefined') PackagesUI.render();
      return DWDeps.get(runtime);
    },
    saveRevision: async () => {
      const b = backendCfg();
      if (!b) { print('// set the backend URL + API key first', 'err'); return null; }
      const path = Tabs.active() || VFS.list()[0];
      if (!path) { print('// no file to save', 'err'); return null; }
      const lang = langForPath(path);
      const runtime = BACKEND_RUNTIME[lang.id];
      if (!runtime) { print(`// ${lang.name}: not a sandbox runtime`, 'err'); return null; }
      const deps = DWDeps.get(runtime);
      const id = DWDeps.submission();
      const url = b.url.replace(/\/$/, '') + (id ? '/execute' : '/submissions');
      const body = id
        ? { runtime, files: workspaceFiles(path), dependencies: deps, submissionId: id }
        : { runtime, files: workspaceFiles(path), dependencies: deps };
      try {
        const res = await fetch(url, { method: 'POST', headers: backendHeaders(b), body: JSON.stringify(body) });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) { print(`revision error ${res.status}: ${data.error || 'request failed'}`, 'err'); return null; }
        if (!id && data.id) DWDeps.setSubmission(data.id);
        const n = id ? data.revision : 1;
        print(`// saved revision ${n}${id ? '' : ' (new submission)'}`, 'ok');
        return { id: DWDeps.submission(), revision: n };
      } catch (e) {
        print('backend unreachable: ' + ((e && e.message) || e), 'err');
        return null;
      }
    },
    compareRevisions: async (from, to) => {
      const b = backendCfg();
      const id = DWDeps.submission();
      if (!b || !id) { print('// no saved submission to compare', 'err'); return null; }
      try {
        const res = await fetch(`${b.url.replace(/\/$/, '')}/submissions/${id}/compare?from=${from}&to=${to}`, {
          headers: backendHeaders(b),
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok) { print(`compare error ${res.status}: ${data.error || 'request failed'}`, 'err'); return null; }
        if (typeof showBottom === 'function') showBottom('output');
        print(`— revision ${from} → ${to} (${data.added} added, ${data.removed} removed)`, 'ok');
        (data.files || []).forEach((f) => {
          if (f.status === 'unchanged') return;
          print(`${f.status}: ${f.path}`, 'ok');
          (f.lines || []).forEach((l) => {
            const cls = l.kind === 'add' ? 'ok' : l.kind === 'del' ? 'err' : undefined;
            print((l.kind === 'add' ? '+ ' : l.kind === 'del' ? '- ' : '  ') + l.text, cls);
          });
        });
        if (data.verdict && data.verdict.after) {
          print(`// verdict: ${data.verdict.after.passed}/${data.verdict.after.total} (delta ${data.verdict.deltaPassed})`, 'ok');
        }
        return data;
      } catch (e) {
        print('backend unreachable: ' + ((e && e.message) || e), 'err');
        return null;
      }
    },
    version: '0.2.0',
  };

  const first = VFS.list().includes('README.md') ? 'README.md' : VFS.list()[0];
  if (first) Tabs.openFile(first);
  else { current = registry.find((l) => l.id === 'python') || registry[0]; updateBadge(); }

  Explorer.render();
  Workspaces.render();
  StatusBar.workspace(Workspaces.current());
  StatusBar.branch('no repo');
  StatusBar.problems(0);
  StatusBar.testsIdle();
  StatusBar.lang(currentLang().name);
  DocsUI.render();
  PackagesUI.render();
  TestsUI.render();
  SettingsUI.init();
  renderProblems();
}

(async function boot() {
  try {
    registry = await (await fetch('languages.json')).json();
  } catch (e) {
    outEl().textContent = 'Failed to load languages.json: ' + e;
    return;
  }
  VFS.load();
  seedIfEmpty();
  populateSelect();
  require(['vs/editor/editor.main'], createEditor);
})();
