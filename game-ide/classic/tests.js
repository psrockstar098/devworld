/* DEVWORLD IDE — test runner.
   Per-file test cases {name, stdin, expected} stored in localStorage.
   "Run tests" executes each through a CAPTURING variant of the file's
   language runner, compares trimmed stdout, and renders a readable report.
   Results are also exposed via GameIDE.runTests() for the game shell
   (mission/quest verification — "Proof of Skill"). */

const TESTS_KEY = 'devworld.tests.v1';

const TestStore = {
  all() {
    try { return JSON.parse(localStorage.getItem(TESTS_KEY) || '{}'); } catch (e) { return {}; }
  },
  save(a) { try { localStorage.setItem(TESTS_KEY, JSON.stringify(a)); } catch (e) {} },
  forFile(path) { return this.all()[VFS.norm(path)] || []; },
  setForFile(path, list) {
    const a = this.all();
    a[VFS.norm(path)] = list;
    this.save(a);
  },
};

/* ---------------- capturing runners (stdout -> string) ---------------- */

const Capture = {
  async python(code, stdin) {
    const py = await ensurePyodide();
    let out = '';
    const data = (stdin || '') + '\n';
    let pos = 0;
    py.setStdout({ batched: (s) => { out += s + '\n'; } });
    py.setStderr({ batched: (s) => { out += s + '\n'; } });
    if (typeof py.setStdin === 'function') {
      py.setStdin({
        stdin: () => (pos < data.length ? data.charCodeAt(pos++) : null),
        isatty: false,
      });
    }
    try {
      await py.runPythonAsync(code);
    } catch (e) {
      out += 'Error: ' + (e && e.message || e) + '\n';
    }
    return out;
  },

  async javascript(code, stdin) {
    const lines = (stdin || '').split('\n');
    let li = 0;
    let out = '';
    const fakeConsole = {
      log: (...a) => { out += a.map(String).join(' ') + '\n'; },
      error: (...a) => { out += a.map(String).join(' ') + '\n'; },
      warn: (...a) => { out += a.map(String).join(' ') + '\n'; },
    };
    const prompt = () => (li < lines.length ? lines[li++] : '');
    const AsyncFn = Object.getPrototypeOf(async function () {}).constructor;
    try {
      const clean = code.replace(/^#!.*(?:\r?\n|$)/, '');
      const fn = new AsyncFn('console', 'prompt', 'readline', '"use strict";\n' + clean);
      await fn(fakeConsole, prompt, prompt);
    } catch (e) {
      out += 'Error: ' + (e && e.message || e) + '\n';
    }
    return out;
  },

  async typescript(code, stdin) {
    if (typeof ts === 'undefined') return 'Error: TypeScript compiler failed to load.\n';
    try {
      const out = ts.transpileModule(code, {
        compilerOptions: { module: ts.ModuleKind.None, target: ts.ScriptTarget.ES2020 },
      });
      return await this.javascript(out.outputText, stdin);
    } catch (e) {
      return 'Error: ' + (e && e.message || e) + '\n';
    }
  },

  async sql(code) {
    const SQL = await ensureSqlJsCtor();
    const db = new SQL.Database();
    let out = '';
    try {
      const results = db.exec(code);
      results.forEach((r) => {
        out += r.columns.join(' | ') + '\n';
        r.values.forEach((row) => { out += row.map((v) => String(v)).join(' | ') + '\n'; });
      });
    } catch (e) {
      out += 'Error: ' + (e && e.message || e) + '\n';
    } finally {
      try { db.close(); } catch (e) {}
    }
    return out;
  },

  lua(code, stdin) {
    const { lua, lauxlib, lualib, to_luastring } = fengari;
    const L = lauxlib.luaL_newstate();
    lualib.luaL_openlibs(L);
    lua.lua_pushstring(L, to_luastring(stdin || ''));
    lua.lua_setglobal(L, '__dw_stdin');
    const prefix =
      '__out={}\nprint=function(...) local t={} ' +
      'for i=1,select("#",...) do t[i]=tostring(select(i,...)) end ' +
      '__out[#__out+1]=table.concat(t,"\\t") end\n' +
      '__dw_lines={} for s in (__dw_stdin.."\\n"):gmatch("(.-)\\n") do ' +
      '__dw_lines[#__dw_lines+1]=s end\n__dw_i=0\n' +
      'io.read=function() __dw_i=__dw_i+1 return __dw_lines[__dw_i] end\n';
    let out = '';
    const st = lauxlib.luaL_dostring(L, to_luastring(prefix + code));
    if (st !== lua.LUA_OK) return 'Error: ' + lua.lua_tojsstring(L, -1) + '\n';
    lua.lua_getglobal(L, '__out');
    const n = lua.lua_rawlen(L, -1);
    for (let i = 1; i <= n; i++) {
      lua.lua_rawgeti(L, -1, i);
      out += lua.lua_tojsstring(L, -1) + '\n';
      lua.lua_pop(L, 1);
    }
    return out;
  },

  async extra(langId, code, stdin) {
    // ruby / php / r live in runners-extra.js (loaded by the child agent).
    const cap = window.EXTRA_CAPTURE && window.EXTRA_CAPTURE[langId];
    if (!cap) return 'Error: runner "' + langId + '" is not available in this build.\n';
    try {
      return await cap(code, stdin || '');
    } catch (e) {
      return 'Error: ' + (e && e.message || e) + '\n';
    }
  },
};

async function captureRun(langId, code, stdin) {
  if (Capture[langId]) return Capture[langId](code, stdin || '');
  return Capture.extra(langId, code, stdin || '');
}

/* ---------------- test execution + report ---------------- */

function firstDiff(aLines, bLines) {
  const n = Math.max(aLines.length, bLines.length);
  for (let i = 0; i < n; i++) {
    if ((aLines[i] || '') !== (bLines[i] || '')) return i;
  }
  return -1;
}

async function runTestsFor(path, code, onProgress) {
  const lang = langForPath(path);
  const cases = TestStore.forFile(path);
  // sandbox-backed languages: grade through the assessment API when a backend is set
  if (lang && lang.run === 'server' && BACKEND_RUNTIME[lang.id] && backendCfg()) {
    return await assessViaBackend(path, code, lang, cases, onProgress);
  }
  const results = [];
  for (const tc of cases) {
    if (onProgress) onProgress(tc.name);
    let actual = '';
    let crashed = false;
    if (!lang || lang.run !== 'wasm' || (!RUNNERS[lang.runner] && !(window.EXTRA_RUNNERS && window.EXTRA_RUNNERS[lang.runner]))) {
      actual = `Error: ${lang ? lang.name : '?'} has no in-browser runner yet.\n`;
      crashed = true;
    } else {
      try {
        actual = await captureRun(lang.runner, code, tc.stdin || '');
      } catch (e) {
        actual = 'Error: ' + (e && e.message || e) + '\n';
        crashed = true;
      }
    }
    const pass = !crashed && actual.trim() === (tc.expected || '').trim();
    results.push({ name: tc.name, pass, actual, expected: tc.expected || '', crashed });
  }
  return results;
}

async function assessViaBackend(path, code, lang, cases, onProgress) {
  const b = backendCfg();
  let data = null;
  try {
    const res = await fetch(b.url.replace(/\/$/, '') + '/assess', {
      method: 'POST',
      headers: backendHeaders(b),
      body: JSON.stringify({
        runtime: BACKEND_RUNTIME[lang.id],
        files: workspaceFiles(path, code),
        tests: cases.map((tc) => ({
          name: tc.name,
          stdin: tc.stdin || '',
          expected_stdout: tc.expected || '',
          hidden: !!tc.hidden,
        })),
        timeout: 30,
      }),
    });
    data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error('backend error ' + res.status);
  } catch (e) {
    return cases.map((tc) => ({
      name: tc.name, pass: false, actual: 'Error: ' + ((e && e.message) || e),
      expected: tc.expected || '', crashed: true,
    }));
  }
  const byName = {};
  (data.results || []).forEach((r) => { byName[r.name] = r; });
  return cases.map((tc) => {
    const r = byName[tc.name] || {};
    const hidden = !!tc.hidden;
    if (onProgress) onProgress(tc.name);
    return {
      name: tc.name,
      pass: !!r.passed,
      hidden,
      actual: hidden ? '(hidden test — result only)' : (r.actual != null ? String(r.actual) : ''),
      expected: hidden ? '' : (tc.expected || ''),
      crashed: !!r.errorKind,
      diagnostics: hidden ? '' : (r.error || ''),
    };
  });
}

function renderReport(results) {
  const host = document.getElementById('testReport');
  host.innerHTML = '';
  if (!results.length) {
    host.innerHTML = '<p class="subtle" style="padding:6px">No test cases yet — add one above.</p>';
    return { passed: 0, total: 0 };
  }
  let passed = 0;
  results.forEach((r) => {
    if (r.pass) passed++;
    const div = document.createElement('div');
    div.className = 'testres ' + (r.pass ? 'pass' : 'fail');
    const head = document.createElement('div');
    head.innerHTML = `<b>${r.pass ? 'PASS' : 'FAIL'}</b> — `;
    head.appendChild(document.createTextNode(r.name));
    div.appendChild(head);
    if (!r.pass && !r.hidden) {
      const aLines = r.actual.trimEnd().split('\n');
      const eLines = r.expected.trimEnd().split('\n');
      const di = firstDiff(aLines, eLines);
      const d = document.createElement('div');
      d.className = 'diff';
      const block = (arr, cls, sign) =>
        arr.slice(0, 10).map((l, i) =>
          `<span class="${cls}">${sign} ${escHtml(l)}${i === di ? '   ← first difference' : ''}</span>`
        ).join('\n');
      d.innerHTML = block(eLines, 'add', '+ expected') + '\n' + block(aLines, 'del', '- actual');
      div.appendChild(d);
    }
    host.appendChild(div);
  });
  const sum = document.createElement('p');
  sum.className = 'subtle';
  sum.style.cssText = 'margin:6px 0;font-weight:700';
  sum.textContent = `${passed}/${results.length} passed`;
  host.prepend(sum);
  const bar = document.createElement('div');
  bar.className = 'pbar' + (passed === results.length ? '' : ' fail');
  bar.innerHTML = `<i style="width:${results.length ? Math.round((passed / results.length) * 100) : 0}%"></i>`;
  host.prepend(bar);
  return { passed, total: results.length };
}

/* ---------------- panel UI ---------------- */

const TestsUI = {
  render() {
    const path = Tabs.active();
    document.getElementById('testsFile').textContent = path ? '· ' + path : '';
    const host = document.getElementById('testList');
    host.innerHTML = '';
    if (!path) { host.innerHTML = '<p class="subtle">Open a file first.</p>'; return; }
    TestStore.forFile(path).forEach((tc, i) => {
      const div = document.createElement('div');
      div.className = 'testcase';
      div.innerHTML = `
        <div style="display:flex;justify-content:space-between;align-items:center">
          <span class="tname"></span>
          <button class="ghost small" data-del>delete</button>
        </div>
        <input class="sinput" data-f="name" placeholder="test name" value="">
        <textarea class="sinput" data-f="stdin" rows="2" placeholder="stdin (optional)"></textarea>
        <textarea class="sinput" data-f="expected" rows="3" placeholder="expected stdout"></textarea>`;
      div.querySelector('.tname').textContent = tc.name || `test ${i + 1}`;
      div.querySelector('[data-f=name]').value = tc.name || '';
      div.querySelector('[data-f=stdin]').value = tc.stdin || '';
      div.querySelector('[data-f=expected]').value = tc.expected || '';
      ['name', 'stdin', 'expected'].forEach((f) => {
        div.querySelector(`[data-f=${f}]`).addEventListener('input', () => this.persist(path));
      });
      div.querySelector('[data-del]').addEventListener('click', () => {
        const list = TestStore.forFile(path);
        list.splice(i, 1);
        TestStore.setForFile(path, list);
        this.render();
      });
      host.appendChild(div);
    });
  },

  persist(path) {
    const current = TestStore.forFile(path);
    const list = [];
    document.querySelectorAll('#testList .testcase').forEach((div, i) => {
      // Edit only the visible fields; everything else on the stored entry
      // (above all the `hidden` flag on seeded tests) is kept as-is.
      const keep = current[i] || {};
      list.push({
        ...keep,
        name: div.querySelector('[data-f=name]').value,
        stdin: div.querySelector('[data-f=stdin]').value,
        expected: div.querySelector('[data-f=expected]').value,
      });
    });
    TestStore.setForFile(path, list);
  },

  add() {
    const path = Tabs.active();
    if (!path) return;
    const list = TestStore.forFile(path);
    list.push({ name: `test ${list.length + 1}`, stdin: '', expected: '' });
    TestStore.setForFile(path, list);
    this.render();
  },

  async runAll() {
    const path = Tabs.active();
    if (!path) { print('Open a file to test.'); return { passed: 0, total: 0 }; }
    const cases = TestStore.forFile(path);
    if (!cases.length) {
      document.getElementById('testReport').innerHTML =
        '<p class="subtle" style="padding:6px">No test cases — add one first.</p>';
      return { passed: 0, total: 0 };
    }
    saveActiveFile();
    showBottom('output');
    clearOut();
    print(`— running ${cases.length} test(s) for ${path}`, 'ok');
    if (typeof StatusBar !== 'undefined') StatusBar.testsRunning();
    const t0 = performance.now();
    const results = await runTestsFor(path, editor.getValue());
    const ms = performance.now() - t0;
    const summary = renderReport(results);
    if (typeof StatusBar !== 'undefined') StatusBar.testsDone(summary.passed, summary.total, ms);
    print(`— ${summary.passed}/${summary.total} passed`, summary.passed === summary.total ? 'ok' : 'err');
    testCallbacks.forEach((cb) => { try { cb(path, summary); } catch (e) {} });
    return summary;
  },
};

const testCallbacks = [];
