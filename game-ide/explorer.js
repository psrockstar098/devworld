/* DEVWORLD IDE — virtual file system, explorer tree, tabs, search, workspaces.
   In-memory file tree persisted to localStorage. Loaded before ide.js. */

const VFS_KEY = 'devworld.vfs.v1';
const WS_KEY = 'devworld.workspaces.v1';
const WS_CUR_KEY = 'devworld.workspace.current';

const VFS = {
  files: {}, // path -> { content }
  listeners: [],

  onChange(cb) { this.listeners.push(cb); },
  emit(type, path) {
    this.listeners.forEach((cb) => { try { cb({ type, path }); } catch (e) {} });
    this.persistSoon();
  },

  norm(p) { return String(p).replace(/^\/+/, '').replace(/\/+/g, '/'); },
  read(path) { const f = this.files[this.norm(path)]; return f ? f.content : null; },
  exists(path) { return this.norm(path) in this.files; },

  write(path, content) {
    path = this.norm(path);
    this.files[path] = { content: String(content) };
    this.emit('write', path);
  },

  remove(path) {
    path = this.norm(path);
    let n = 0;
    Object.keys(this.files).forEach((k) => {
      if (k === path || k.startsWith(path + '/')) { delete this.files[k]; n++; }
    });
    if (n) this.emit('remove', path);
    return n;
  },

  rename(oldPath, newPath) {
    oldPath = this.norm(oldPath); newPath = this.norm(newPath);
    if (oldPath === newPath || !(oldPath in this.files)) return false;
    this.files[newPath] = this.files[oldPath];
    delete this.files[oldPath];
    this.emit('rename', newPath);
    return true;
  },

  renameDir(oldDir, newDir) {
    oldDir = this.norm(oldDir); newDir = this.norm(newDir);
    let n = 0;
    Object.keys(this.files).forEach((k) => {
      if (k === oldDir || k.startsWith(oldDir + '/')) {
        const nk = newDir + k.slice(oldDir.length);
        this.files[nk] = this.files[k]; delete this.files[k]; n++;
      }
    });
    if (n) this.emit('rename', newDir);
    return n;
  },

  list() { return Object.keys(this.files).sort(); },

  dirs() {
    const d = new Set();
    Object.keys(this.files).forEach((p) => {
      const parts = p.split('/');
      for (let i = 1; i < parts.length; i++) d.add(parts.slice(0, i).join('/'));
    });
    return [...d].sort();
  },

  children(dir) {
    // direct children (files and immediate subdirs) of dir ('' = root)
    const files = [], dirs = new Set();
    Object.keys(this.files).forEach((p) => {
      if (dir && !(p === dir || p.startsWith(dir + '/'))) return;
      const rest = dir ? p.slice(dir.length + 1) : p;
      if (!rest) return;
      const seg = rest.split('/');
      if (seg.length === 1) files.push(p);
      else dirs.add(dir ? dir + '/' + seg[0] : seg[0]);
    });
    return { files: files.sort(), dirs: [...dirs].sort() };
  },

  ext(path) {
    const base = path.split('/').pop();
    const i = base.lastIndexOf('.');
    return i > 0 ? base.slice(i + 1).toLowerCase() : '';
  },

  _t: null,
  persistSoon() {
    clearTimeout(this._t);
    this._t = setTimeout(() => this.persist(), 400);
  },
  persist() {
    try {
      localStorage.setItem(VFS_KEY, JSON.stringify(this.files));
    } catch (e) { /* quota — keep in memory */ }
  },
  load() {
    try {
      const raw = localStorage.getItem(VFS_KEY);
      if (raw) { this.files = JSON.parse(raw) || {}; return true; }
    } catch (e) {}
    return false;
  },
  snapshot() { return JSON.parse(JSON.stringify(this.files)); },
  restore(snap) {
    this.files = JSON.parse(JSON.stringify(snap));
    this.emit('restore', '');
  },
};

/* ---------------- tabs ---------------- */

const Tabs = {
  open: [],   // paths
  activePath: null,
  onSwitch: [],

  active() { return this.activePath; },
  list() { return this.open.slice(); },
  isOpen(p) { return this.open.includes(p); },

  openFile(path) {
    path = VFS.norm(path);
    if (!VFS.exists(path)) return;
    if (!this.open.includes(path)) this.open.push(path);
    this.switchTo(path);
  },

  switchTo(path) {
    if (this.activePath === path) return;
    this.activePath = path;
    this.render();
    this.onSwitch.forEach((cb) => { try { cb(path); } catch (e) {} });
  },

  close(path) {
    const i = this.open.indexOf(path);
    if (i < 0) return;
    this.open.splice(i, 1);
    if (this.activePath === path) {
      this.activePath = this.open[Math.min(i, this.open.length - 1)] || null;
      this.onSwitch.forEach((cb) => { try { cb(this.activePath); } catch (e) {} });
    }
    this.render();
  },

  onRename(oldP, newP) {
    const i = this.open.indexOf(oldP);
    if (i >= 0) this.open[i] = newP;
    if (this.activePath === oldP) this.activePath = newP;
    this.render();
  },
  onRemove(path) {
    [...this.open].forEach((p) => {
      if (p === path || p.startsWith(path + '/')) this.close(p);
    });
  },

  isDirty(path) {
    // dirty = editor content differs from saved VFS content (only meaningful for active file)
    if (path !== this.activePath || typeof editor === 'undefined' || !editor) return false;
    return editor.getValue() !== (VFS.read(path) || '');
  },

  render() {
    const bar = document.getElementById('tabbar');
    bar.innerHTML = '';
    this.open.forEach((p) => {
      const t = document.createElement('div');
      t.className = 'tab' + (p === this.activePath ? ' active' : '');
      const name = p.split('/').pop();
      const dot = this.isDirty(p) ? '<span class="dot" title="unsaved">•</span>' : '';
      t.innerHTML = `${badgeFor(p)}<span></span>${dot}<span class="x" title="Close">×</span>`;
      t.children[1].textContent = name;
      t.title = p;
      t.addEventListener('click', (e) => {
        if (e.target.classList.contains('x')) this.close(p);
        else this.switchTo(p);
      });
      bar.appendChild(t);
    });
    this.refreshDirty();
  },

  refreshDirty() {
    // update dirty dots without full re-render
    const bar = document.getElementById('tabbar');
    [...bar.children].forEach((el, i) => {
      const p = this.open[i];
      const has = el.querySelector('.dot');
      if (this.isDirty(p) && !has) {
        const s = document.createElement('span');
        s.className = 'dot'; s.title = 'unsaved'; s.textContent = '•';
        el.insertBefore(s, el.querySelector('.x'));
      } else if (!this.isDirty(p) && has) has.remove();
    });
  },
};

/* ---------------- explorer tree UI ---------------- */

const Explorer = {
  collapsed: new Set(),

  render() {
    const host = document.getElementById('filetree');
    host.innerHTML = '';
    this.renderDir(host, '', 0);
    if (!VFS.list().length) {
      host.innerHTML = '<p class="subtle" style="padding:6px">No files yet — use +F / +P.</p>';
    }
  },

  renderDir(host, dir, depth) {
    const { files, dirs } = VFS.children(dir);
    dirs.forEach((d) => {
      const name = d.split('/').pop();
      const row = document.createElement('div');
      row.className = 'ft-dir';
      const open = !this.collapsed.has(d);
      row.textContent = (open ? '▾ ' : '▸ ') + name;
      row.title = d;
      row.addEventListener('click', () => {
        if (this.collapsed.has(d)) this.collapsed.delete(d); else this.collapsed.add(d);
        this.render();
      });
      row.addEventListener('contextmenu', (e) => { e.preventDefault(); this.dirMenu(d); });
      host.appendChild(row);
      if (open) {
        const wrap = document.createElement('div');
        wrap.className = 'ft-children';
        this.renderDir(wrap, d, depth + 1);
        host.appendChild(wrap);
      }
    });
    files.forEach((p) => {
      const row = document.createElement('div');
      row.className = 'ft-file' + (p === Tabs.active() ? ' active' : '');
      row.innerHTML = badgeFor(p);
      const nm = document.createElement('span');
      nm.className = 'fname';
      nm.textContent = p.split('/').pop();
      row.appendChild(nm);
      if (Tabs.isDirty(p)) {
        const d = document.createElement('span');
        d.className = 'dirtydot'; d.textContent = '•';
        row.appendChild(d);
      }
      row.title = p;
      row.addEventListener('click', () => Tabs.openFile(p));
      row.addEventListener('contextmenu', (e) => { e.preventDefault(); this.fileMenu(p); });
      host.appendChild(row);
    });
  },

  askName(promptText, initial, cb) {
    // small inline modal instead of window.prompt
    const modal = document.getElementById('modal');
    const box = document.getElementById('modalBox');
    box.innerHTML = `<h3>${promptText}</h3>
      <input id="mInput" class="sinput" value=""> <div style="margin-top:10px;display:flex;gap:8px;justify-content:flex-end">
      <button id="mCancel" class="ghost">Cancel</button><button id="mOk">OK</button></div>`;
    const inp = box.querySelector('#mInput');
    inp.value = initial || '';
    modal.classList.remove('hidden');
    setTimeout(() => { inp.focus(); inp.select(); }, 30);
    const done = (v) => { modal.classList.add('hidden'); if (v !== null) cb(v); };
    box.querySelector('#mOk').onclick = () => done(inp.value.trim());
    box.querySelector('#mCancel').onclick = () => done(null);
    inp.onkeydown = (e) => { if (e.key === 'Enter') done(inp.value.trim()); if (e.key === 'Escape') done(null); };
  },

  fileMenu(p) {
    const modal = document.getElementById('modal');
    const box = document.getElementById('modalBox');
    box.innerHTML = `<h3></h3>
      <div style="display:flex;gap:8px;margin-top:6px">
        <button id="mRename">Rename</button>
        <button id="mDelete" class="danger">Delete</button>
        <button id="mCancel" class="ghost">Cancel</button>
      </div>`;
    box.querySelector('h3').textContent = p;
    modal.classList.remove('hidden');
    const close = () => modal.classList.add('hidden');
    box.querySelector('#mCancel').onclick = close;
    box.querySelector('#mRename').onclick = () => {
      close();
      const old = p.split('/').pop();
      this.askName('Rename file', old, (name) => {
        if (!name || name === old) return;
        const dir = p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '';
        const np = dir ? dir + '/' + name : name;
        if (VFS.exists(np)) return;
        VFS.rename(p, np);
        Tabs.onRename(p, np);
        if (typeof refreshEditorForPath === 'function') refreshEditorForPath();
      });
    };
    box.querySelector('#mDelete').onclick = () => {
      close();
      if (Tabs.active() === p) { /* keep editor, will clear */ }
      this.deletePath(p);
      if (typeof refreshEditorForPath === 'function') refreshEditorForPath();
    };
  },

  dirMenu(d) {
    this.askName('Rename folder', d.split('/').pop(), (name) => {
      if (!name) return;
      const parent = d.includes('/') ? d.slice(0, d.lastIndexOf('/')) : '';
      const nd = parent ? parent + '/' + name : name;
      VFS.renameDir(d, nd);
      // fix open tabs
      Tabs.list().forEach((p) => {
        if (p === d || p.startsWith(d + '/')) Tabs.onRename(p, nd + p.slice(d.length));
      });
      Explorer.render();
    });
  },

  newFile() {
    this.askName('New file name', 'main.py', (name) => {
      if (!name) return;
      const p = VFS.norm(name);
      if (VFS.exists(p)) { Tabs.openFile(p); return; }
      VFS.write(p, '');
      Tabs.openFile(p);
    });
  },

  newFolder() {
    this.askName('New folder name', 'src', (name) => {
      if (!name) return;
      const p = VFS.norm(name) + '/.keep';
      VFS.write(p, '');
      this.render();
    });
  },

  deletePath(p) {
    VFS.remove(p);
    Tabs.onRemove(p);
    this.render();
  },
};

/* ---------------- language badges (per-language file icons) ---------------- */

const LANG_BADGE = {
  python: ['Py', '#4b8bbe'], javascript: ['JS', '#e8c332'], typescript: ['TS', '#4b9cd3'],
  sql: ['SQ', '#e8a13c'], lua: ['Lu', '#9aa5ff'], ruby: ['Rb', '#e06c5b'], php: ['Ph', '#9a9fd3'],
  r: ['R', '#5b9bd5'], c: ['C', '#9aa5b1'], cpp: ['C+', '#f34b7d'], csharp: ['C#', '#7fb069'],
  java: ['Jv', '#e76f00'], go: ['Go', '#3fb6d3'], rust: ['Rs', '#dea584'], kotlin: ['Kt', '#8f7bd3'],
  swift: ['Sw', '#f05138'], html: ['H5', '#e34f26'], css: ['C3', '#4b9cd3'], zig: ['Zg', '#f7a41d'],
  dart: ['Dt', '#4b9cd3'], json: ['Js', '#8b949e'], markdown: ['Md', '#8b949e'], bash: ['Sh', '#3fb950'],
  shell: ['Sh', '#3fb950'],
};

function badgeFor(path) {
  let id = null;
  try { if (typeof langForPath === 'function') { const l = langForPath(path); id = l && l.id; } } catch (e) {}
  if (!id) {
    const ext = VFS.ext(path);
    if (ext === 'md') id = 'markdown';
    else if (ext === 'json') id = 'json';
    else if (ext === 'html' || ext === 'htm') id = 'html';
    else if (ext === 'css') id = 'css';
    else if (ext === 'sh') id = 'bash';
  }
  const b = LANG_BADGE[id];
  if (b) return `<span class="lbadge" style="--c:${b[1]}">${b[0]}</span>`;
  const fb = (id || VFS.ext(path) || '?').slice(0, 2).toUpperCase();
  return `<span class="lbadge">${escHtml(fb)}</span>`;
}

/* ---------------- status bar pills (bottom) ---------------- */

const StatusBar = {
  set(id, text, cls) {
    const el = document.getElementById('sb' + id[0].toUpperCase() + id.slice(1));
    if (!el) return;
    el.className = 'pill' + (cls ? ' ' + cls : '');
    el.innerHTML = '<span class="dot"></span><span></span>';
    el.lastChild.textContent = text;
  },
  workspace(n) { this.set('workspace', n, 'accent'); },
  branch(b) { this.set('branch', b); },
  lang(name) { this.set('lang', name || '—'); },
  problems(n) { this.set('problems', n + (n === 1 ? ' problem' : ' problems'), n ? 'warn' : ''); },
  testsRunning() { this.set('tests', 'Tests running…', 'warn'); },
  testsDone(passed, total, ms) {
    const ok = passed === total;
    this.set('tests', `Tests ${passed}/${total} ${ok ? 'passed' : 'failed'} · ${(ms / 1000).toFixed(1)}s`, ok ? 'ok' : 'fail');
  },
  testsIdle() { this.set('tests', 'Tests —', ''); },
};

/* ---------------- settings view ---------------- */

const SettingsUI = {
  init() {
    const b = (typeof GameIDE !== 'undefined' && GameIDE.getBackend) ? GameIDE.getBackend() : null;
    const urlEl = document.getElementById('setBackendUrl');
    const keyEl = document.getElementById('setBackendKey');
    const stateEl = document.getElementById('setBackendState');
    if (b) { urlEl.value = b.url || ''; }
    const showState = () => {
      const cur = (typeof GameIDE !== 'undefined' && GameIDE.getBackend) ? GameIDE.getBackend() : null;
      stateEl.textContent = cur && cur.url ? 'Configured: ' + cur.url : 'Not configured.';
    };
    showState();
    document.getElementById('setBackendSave').addEventListener('click', () => {
      if (typeof GameIDE !== 'undefined' && GameIDE.setBackend) {
        GameIDE.setBackend({ url: urlEl.value.trim(), apiKey: keyEl.value });
        keyEl.value = '';
        showState();
        if (typeof updateBadge === 'function') updateBadge();
      }
    });
    document.getElementById('setBackendClear').addEventListener('click', () => {
      try { localStorage.removeItem('devworld.backend.v1'); } catch (e) {}
      urlEl.value = ''; keyEl.value = '';
      showState();
      if (typeof updateBadge === 'function') updateBadge();
    });
    document.getElementById('setFontSize').addEventListener('change', (e) => {
      const n = Math.max(10, Math.min(24, parseInt(e.target.value, 10) || 14));
      if (typeof editor !== 'undefined' && editor) editor.updateOptions({ fontSize: n });
      try { localStorage.setItem('devworld.fontsize', String(n)); } catch (err) {}
    });
    try {
      const fs = localStorage.getItem('devworld.fontsize');
      if (fs) document.getElementById('setFontSize').value = fs;
    } catch (e) {}
    document.getElementById('setWipe').addEventListener('click', () => {
      if (!window.confirm('Erase ALL local IDE data (files, workspaces, tests, git history)?')) return;
      Object.keys(localStorage).filter((k) => k.startsWith('devworld.')).forEach((k) => localStorage.removeItem(k));
      location.reload();
    });
  },
};

const Search = {
  run(q) {
    const host = document.getElementById('searchResults');
    host.innerHTML = '';
    q = q.trim().toLowerCase();
    if (q.length < 2) { host.innerHTML = '<p class="subtle" style="padding:6px">Type at least 2 characters.</p>'; return; }
    let total = 0;
    VFS.list().forEach((p) => {
      const content = VFS.read(p);
      content.split('\n').forEach((line, i) => {
        const idx = line.toLowerCase().indexOf(q);
        if (idx >= 0 && total < 200) {
          total++;
          const div = document.createElement('div');
          div.className = 'srch-hit';
          const f = document.createElement('div');
          f.className = 'f'; f.textContent = `${p}:${i + 1}`;
          const c = document.createElement('code');
          c.textContent = line.trim().slice(0, 120);
          div.appendChild(f); div.appendChild(c);
          div.addEventListener('click', () => {
            Tabs.openFile(p);
            if (typeof revealLine === 'function') revealLine(i + 1);
          });
          host.appendChild(div);
        }
      });
    });
    if (!total) host.innerHTML = '<p class="subtle" style="padding:6px">No matches.</p>';
  },
};

/* ---------------- workspaces ---------------- */

const Workspaces = {
  all() {
    try { return JSON.parse(localStorage.getItem(WS_KEY) || '{}'); } catch (e) { return {}; }
  },
  saveAll(w) { try { localStorage.setItem(WS_KEY, JSON.stringify(w)); } catch (e) {} },
  current() { return localStorage.getItem(WS_CUR_KEY) || 'default'; },
  setCurrent(n) { try { localStorage.setItem(WS_CUR_KEY, n); } catch (e) {} },

  save(name) {
    const w = this.all();
    w[name] = { files: VFS.snapshot(), savedAt: Date.now() };
    this.saveAll(w);
    this.setCurrent(name);
    if (typeof StatusBar !== 'undefined') StatusBar.workspace(name);
    this.render();
  },
  load(name) {
    const w = this.all();
    if (!w[name]) return;
    // stash current workspace first
    const cur = this.current();
    if (cur && cur !== name) {
      const all = this.all();
      all[cur] = { files: VFS.snapshot(), savedAt: Date.now() };
      this.saveAll(all);
    }
    VFS.restore(w[name].files);
    this.setCurrent(name);
    if (typeof StatusBar !== 'undefined') StatusBar.workspace(name);
    Tabs.open = []; Tabs.activePath = null;
    const first = VFS.list()[0];
    if (first) Tabs.openFile(first);
    Tabs.render();
    Explorer.render();
    if (typeof refreshEditorForPath === 'function') refreshEditorForPath();
    this.render();
  },
  remove(name) {
    const w = this.all();
    delete w[name];
    this.saveAll(w);
    if (this.current() === name) this.setCurrent('default');
    this.render();
  },

  render() {
    const host = document.getElementById('workspaces');
    host.innerHTML = '';
    const w = this.all();
    const cur = this.current();
    Object.keys(w).sort().forEach((n) => {
      const row = document.createElement('div');
      row.className = 'ws-row' + (n === cur ? ' current' : '');
      const nm = document.createElement('span');
      nm.textContent = n;
      nm.style.cursor = 'pointer';
      nm.title = 'Switch workspace';
      nm.addEventListener('click', () => this.load(n));
      const acts = document.createElement('span');
      const del = document.createElement('button');
      del.className = 'ghost small'; del.textContent = 'del'; del.title = 'Delete workspace';
      del.addEventListener('click', (e) => { e.stopPropagation(); this.remove(n); });
      acts.appendChild(del);
      row.appendChild(nm); row.appendChild(acts);
      host.appendChild(row);
    });
    const nw = document.createElement('div');
    nw.className = 'ws-new';
    nw.innerHTML = '<input class="sinput" id="wsName" placeholder="workspace name" style="margin:0">';
    const btn = document.createElement('button');
    btn.className = 'small'; btn.textContent = 'Save';
    btn.addEventListener('click', () => {
      const v = nw.querySelector('#wsName').value.trim();
      if (v) { this.save(v); }
    });
    nw.appendChild(btn);
    host.appendChild(nw);
  },
};

const WELCOME = `# Welcome to DEVWORLD

This is your game IDE. Everything runs in your browser.

- **Files**: create, rename and organize files in the explorer (right-click a file to rename).
- **Run**: the Run button executes the open file. Python, JavaScript, TypeScript, SQL and Lua run locally right now; more languages unlock later.
- **Tests**: define test cases in the Tests panel and run them against your file.
- **Terminal**: type \`help\` in the Terminal tab below.
- **Git**: the Git panel gives you init, status, commit, log, diff and branches — all local.
- **Docs**: cheat sheets for the main languages live in the Docs panel.

Your files auto-save to this browser. Use Workspaces below to snapshot and switch between projects.

Delete this file whenever you like — your world, your rules.
`;

function seedIfEmpty() {
  if (!VFS.list().length) {
    VFS.write('README.md', WELCOME);
  }
}
