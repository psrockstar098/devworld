/* DEVWORLD IDE — JavaScript debugger.
   Simplified tracer: instruments the source line-by-line, inserting an
   `await __dw_hit(line, scopeGetter)` checkpoint before each statement-start
   line. The scope getter closes over the statement's scope, so the Variables
   panel shows genuinely live locals. Breakpoints toggle by clicking the
   gutter (line numbers); they persist per file in localStorage.

   Honest limitations (documented, not hidden):
   - Only straight-line-friendly code is fully traced; exotic multi-line
     constructs (multiline template literals, etc.) may confuse the naive
     statement splitter — breakpoints on such lines simply won't fire.
   - Async continuations (setTimeout / promises resolving after main returns)
     are not traced; only the synchronous flow is.
   - `import`/`export` are not supported (same as the normal JS runner). */

const BP_KEY = 'devworld.breakpoints.v1';

class DebugStop extends Error {}

const Debug = {
  state: {
    active: false, paused: false, stepping: false, stopRequested: false,
    resume: null, steps: 0, maxSteps: 2000000,
    currentLine: 0, scope: {}, breakpoints: new Set(),
  },
  editor: null,
  decoIds: [],

  load() {
    try {
      const raw = JSON.parse(localStorage.getItem(BP_KEY) || '{}');
      this.bpMap = raw;
    } catch (e) { this.bpMap = {}; }
  },
  save() {
    try { localStorage.setItem(BP_KEY, JSON.stringify(this.bpMap)); } catch (e) {}
  },

  attach(ed) {
    this.editor = ed;
    this.load();
    ed.onMouseDown((e) => {
      if (!e.target || e.target.type !== monaco.editor.MouseTargetType.GUTTER_LINE_NUMBERS) return;
      const path = Tabs.active();
      if (!path) return;
      this.toggle(path, e.target.position.lineNumber);
    });
  },

  list(path) {
    return (this.bpMap[VFS.norm(path)] || []).slice().sort((a, b) => a - b);
  },

  toggle(path, line) {
    path = VFS.norm(path);
    const arr = this.bpMap[path] || [];
    const i = arr.indexOf(line);
    if (i >= 0) arr.splice(i, 1); else arr.push(line);
    if (arr.length) this.bpMap[path] = arr; else delete this.bpMap[path];
    this.save();
    this.refreshDecorations();
  },

  refreshDecorations() {
    if (!this.editor || typeof monaco === 'undefined') return;
    const path = Tabs.active();
    const bps = path ? this.list(path) : [];
    const decos = bps.map((ln) => ({
      range: new monaco.Range(ln, 1, ln, 1),
      options: {
        glyphMarginClassName: 'bp-glyph',
        glyphMarginHoverMessage: { value: 'Breakpoint (click gutter to remove)' },
        stickiness: 1,
      },
    }));
    if (this.state.active && this.state.currentLine) {
      decos.push({
        range: new monaco.Range(this.state.currentLine, 1, this.state.currentLine, 1),
        options: { isWholeLine: true, className: 'debug-line' },
      });
    }
    this.decoIds = this.editor.deltaDecorations(this.decoIds, decos);
    this.state.breakpoints = new Set(bps);
  },

  highlightLine(line) {
    this.state.currentLine = line;
    this.refreshDecorations();
    if (this.editor) this.editor.revealLineInCenterIfOutsideViewport(line);
  },

  renderVariables() {
    const host = document.getElementById('variables');
    const scope = this.state.scope || {};
    const keys = Object.keys(scope);
    if (!keys.length) {
      host.innerHTML = `<p class="subtle">Paused at line ${this.state.currentLine} — no locals captured.</p>`;
      return;
    }
    host.innerHTML = `<p class="subtle" style="margin-bottom:6px">Paused at line ${this.state.currentLine}</p>` +
      keys.map((k) => {
        let v;
        try { v = fmtVal(scope[k]); } catch (e) { v = '?'; }
        return `<div class="var-row"><span class="k">${escHtml(k)}</span> = <span class="v">${escHtml(v)}</span></div>`;
      }).join('');
  },

  updateUI() {
    const on = this.state.active;
    document.getElementById('debugControls').classList.toggle('hidden', !on);
    document.getElementById('varTab').classList.toggle('hidden', !on);
    document.getElementById('debugBtn').disabled = on;
  },

  /* ---------- instrumentation ---------- */

  scanDepth(line, depth) {
    let q = null;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) {
        if (c === '\\') { i++; continue; }
        if (c === q) q = null;
        continue;
      }
      if (c === '"' || c === "'" || c === '`') { q = c; continue; }
      if (c === '/' && line[i + 1] === '/') break;
      if (c === '(' || c === '[' || c === '{') depth++;
      else if (c === ')' || c === ']' || c === '}') depth--;
    }
    return depth;
  },

  isBlockOpener(prevTrim) {
    if (!/\{\s*$/.test(prevTrim)) return false;
    return /\b(if|for|while|switch|with|catch|finally)\s*\(.*\)\s*\{\s*$/.test(prevTrim) ||
      /(^|[}\s])(else|try|do)\s*\{\s*$/.test(prevTrim) ||
      /=>\s*\{\s*$/.test(prevTrim) ||
      /\bfunction\b[^{]*\{\s*$/.test(prevTrim);
  },

  instrument(code) {
    const lines = code.split('\n');
    const out = [];
    const seen = [];
    const seenSet = new Set();
    const addDecls = (line) => {
      let m;
      const re = /\b(?:let|const|var)\s+([A-Za-z_$][\w$]*)/g;
      while ((m = re.exec(line))) if (!seenSet.has(m[1])) { seenSet.add(m[1]); seen.push(m[1]); }
      const fre = /\bfunction\s+([A-Za-z_$][\w$]*)\s*\(/g;
      while ((m = fre.exec(line))) if (!seenSet.has(m[1])) { seenSet.add(m[1]); seen.push(m[1]); }
      const cre = /\bcatch\s*\(\s*([A-Za-z_$][\w$]*)\s*\)/g;
      while ((m = cre.exec(line))) if (!seenSet.has(m[1])) { seenSet.add(m[1]); seen.push(m[1]); }
    };
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      addDecls(line);
      const t = line.trim();
      if (!t || t.startsWith('//')) { out.push(line); continue; }
      const prevRaw = i ? lines[i - 1] : '';
      const prev = prevRaw.trim();
      const elseLike = /^\s*(else|catch|finally)\b/.test(line);
      const doWhileTail = /^\s*while\b/.test(line) && /;\s*$/.test(line);
      const prevEndsOp = /[+\-*/%=,(\[{&|^?:<>!]\s*$/.test(prevRaw);
      const continuation = elseLike || doWhileTail ||
        (prevEndsOp && !this.isBlockOpener(prev));
      if (!continuation) {
        const getter = '() => { const __dw_s = {};' +
          seen.map((n) => `try{__dw_s.${n}=${n};}catch(__dw_e){}`).join('') +
          ' return __dw_s; }';
        out.push(`await __dw_hit(${i + 1}, ${getter});`);
      }
      out.push(line);
    }
    return 'return (async () => {\n' + out.join('\n') + '\n})();';
  },

  /* ---------- session control ---------- */

  async start(path, code) {
    const st = this.state;
    if (st.active) return;
    Object.assign(st, {
      active: true, paused: false, stepping: false, stopRequested: false,
      resume: null, steps: 0, currentLine: 0, scope: {},
    });
    this.refreshDecorations();
    this.updateUI();
    showBottom('output');
    document.getElementById('varTab').classList.remove('hidden');

    const logs = [];
    const fakeConsole = {
      log: (...a) => logs.push(a.map(String).join(' ')),
      error: (...a) => logs.push('[err] ' + a.map(String).join(' ')),
      warn: (...a) => logs.push('[warn] ' + a.map(String).join(' ')),
    };

    const hit = async (line, getScope) => {
      if (!st.active) return;
      st.steps++;
      if (st.steps > st.maxSteps) throw new Error('Execution limit exceeded — possible infinite loop. Stopped.');
      st.currentLine = line;
      try { st.scope = getScope ? getScope() : {}; } catch (e) { st.scope = {}; }
      this.renderVariables();
      this.highlightLine(line);
      if (st.stopRequested) { st.active = false; throw new DebugStop(); }
      if (st.breakpoints.has(line) || st.stepping) {
        st.paused = true;
        this.updateUI();
        showBottom('variables');
        await new Promise((res) => { st.resume = res; });
        st.paused = false;
        st.stepping = false;
        this.updateUI();
        if (st.stopRequested) { st.active = false; throw new DebugStop(); }
      }
    };

    let instrumented;
    try {
      instrumented = this.instrument(code);
    } catch (e) {
      print('debugger: could not instrument code: ' + e.message, 'err');
      this.finish();
      return;
    }

    print(`— debugging ${path} (${this.list(path).length} breakpoint(s))`, 'ok');
    const t0 = performance.now();
    try {
      const fn = new Function('__dw_hit', 'console', '"use strict";\n' + instrumented);
      await fn(hit, fakeConsole);
      logs.forEach((l) => print(l));
      print(`\n— debug finished in ${Math.round(performance.now() - t0)}ms`, 'ok');
    } catch (e) {
      if (e instanceof DebugStop) {
        print('— debugging stopped.', 'ok');
      } else {
        logs.forEach((l) => print(l));
        print('Error at line ' + (st.currentLine || '?') + ': ' + (e && e.message || e), 'err');
      }
    } finally {
      this.finish();
    }
  },

  stepOver() {
    const st = this.state;
    if (!st.active || !st.paused) return;
    st.stepping = true;
    if (st.resume) { const r = st.resume; st.resume = null; r(); }
  },

  continue_() {
    const st = this.state;
    if (!st.active || !st.paused) return;
    st.stepping = false;
    if (st.resume) { const r = st.resume; st.resume = null; r(); }
  },

  stop() {
    const st = this.state;
    if (!st.active) return;
    st.stopRequested = true;
    if (st.resume) { const r = st.resume; st.resume = null; r(); }
    else { st.active = false; this.finish(); }
  },

  finish() {
    const st = this.state;
    st.active = false; st.paused = false; st.currentLine = 0; st.scope = {};
    this.updateUI();
    this.refreshDecorations();
    document.getElementById('variables').innerHTML =
      '<p class="subtle">Start a debug session to inspect variables.</p>';
  },
};

function escHtml(s) {
  return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}
function fmtVal(v) {
  if (v === undefined) return 'undefined';
  if (typeof v === 'string') return JSON.stringify(v);
  if (typeof v === 'function') return 'function ' + (v.name || '(anon)');
  try {
    const s = JSON.stringify(v);
    return s === undefined ? String(v) : (s.length > 120 ? s.slice(0, 120) + '…' : s);
  } catch (e) { return String(v); }
}
