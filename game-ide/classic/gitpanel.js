/* DEVWORLD IDE — source control panel (isomorphic-git, local only).
   Uses a tiny in-memory FS adapter persisted to localStorage. The working
   tree is synced from the IDE's VFS before every operation; there are no
   remotes — init / status / add / commit / log / diff / branches all run
   locally in the browser. */

const GITFS_KEY = 'devworld.gitfs.v1';

function loadScript(url) {
  return new Promise((res, rej) => {
    const s = document.createElement('script');
    s.src = url;
    s.onload = () => res();
    s.onerror = () => rej(new Error('failed to load ' + url));
    document.head.appendChild(s);
  });
}

function enoent(p) { const e = new Error(`ENOENT: no such file or directory, '${p}'`); e.code = 'ENOENT'; return e; }
function enotempty(p) { const e = new Error(`ENOTEMPTY: directory not empty, '${p}'`); e.code = 'ENOTEMPTY'; return e; }

const te = new TextEncoder();
const td = new TextDecoder();

const GitFS = {
  store: {}, // normpath -> Uint8Array (files only; dirs are implicit)

  norm(p) {
    // collapse '.', duplicate and trailing slashes; '' = repo root
    return String(p).split('/').filter((s) => s !== '' && s !== '.').join('/');
  },

  isDir(p) {
    p = this.norm(p);
    if (p === '') return true;
    return Object.keys(this.store).some((k) => k === p || k.startsWith(p + '/'));
  },
  isFile(p) { return this.norm(p) in this.store; },

  _stat(p) {
    p = this.norm(p);
    const dir = this.isDir(p) && !this.isFile(p);
    if (!dir && !this.isFile(p)) throw enoent(p);
    const size = dir ? 0 : this.store[p].length;
    return {
      isDirectory: () => dir,
      isFile: () => !dir,
      isSymbolicLink: () => false,
      mode: dir ? 0o040000 : 0o100644,
      size,
      mtimeMs: Date.now(),
      mtime: new Date(),
      ctimeMs: Date.now(),
      ctime: new Date(),
      dev: 0,
      ino: 0,
    };
  },

  async readFile(p, options) {
    p = this.norm(p);
    if (!(p in this.store)) throw enoent(p);
    const data = this.store[p].slice();
    const enc = typeof options === 'string' ? options : options && options.encoding;
    if (enc === 'utf8' || enc === 'utf-8') return td.decode(data);
    return data;
  },
  async writeFile(p, data) {
    p = this.norm(p);
    if (typeof data === 'string') data = te.encode(data);
    this.store[p] = data instanceof Uint8Array ? data.slice() : Uint8Array.from(data);
    this.persistSoon();
  },
  async unlink(p) {
    p = this.norm(p);
    if (!(p in this.store)) throw enoent(p);
    delete this.store[p];
    this.persistSoon();
  },
  async mkdir(p) { /* dirs are implicit */ },
  async rmdir(p) {
    p = this.norm(p);
    if (!this.isDir(p)) throw enoent(p);
    const kids = Object.keys(this.store).filter((k) => k !== p && k.startsWith(p === '' ? '' : p + '/'));
    if (kids.length) throw enotempty(p);
  },
  async readdir(p) {
    p = this.norm(p);
    if (!this.isDir(p) || (p !== '' && this.isFile(p))) throw enoent(p);
    const out = new Set();
    Object.keys(this.store).forEach((k) => {
      if (p === '') { out.add(k.split('/')[0]); return; }
      if (k === p || !k.startsWith(p + '/')) return;
      out.add(k.slice(p.length + 1).split('/')[0]);
    });
    return [...out].sort();
  },
  async stat(p) { return this._stat(p); },
  async lstat(p) { return this._stat(p); },
  async readlink() { const e = new Error('readlink not supported'); e.code = 'ENOSYS'; throw e; },
  async symlink() { const e = new Error('symlink not supported'); e.code = 'ENOSYS'; throw e; },

  _t: null,
  persistSoon() { clearTimeout(this._t); this._t = setTimeout(() => this.persist(), 500); },
  persist() {
    try {
      const o = {};
      Object.keys(this.store).forEach((k) => {
        const b = this.store[k];
        let s = '';
        for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode.apply(null, b.subarray(i, i + 8192));
        o[k] = btoa(s);
      });
      localStorage.setItem(GITFS_KEY, JSON.stringify(o));
    } catch (e) { /* quota — git history stays in memory */ }
  },
  load() {
    try {
      const raw = localStorage.getItem(GITFS_KEY);
      if (!raw) return;
      const o = JSON.parse(raw);
      Object.keys(o).forEach((k) => {
        const s = atob(o[k]);
        const b = new Uint8Array(s.length);
        for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i);
        this.store[k] = b;
      });
    } catch (e) {}
  },

  // working tree <-> VFS sync (excludes .git)
  syncFromVFS() {
    Object.keys(this.store).forEach((k) => {
      if (!k.startsWith('.git/') && k !== '.git') delete this.store[k];
    });
    VFS.list().forEach((p) => { this.store[p] = te.encode(VFS.read(p)); });
    this.persistSoon();
  },
  syncToVFS() {
    const seen = new Set();
    Object.keys(this.store).forEach((k) => {
      if (k.startsWith('.git/') || k === '.git') return;
      seen.add(k);
      const content = td.decode(this.store[k]);
      if (VFS.read(k) !== content) VFS.write(k, content);
    });
    // files deleted by checkout
    VFS.list().forEach((p) => { if (!seen.has(p)) VFS.remove(p); });
  },
};

async function ensureGit() {
  if (window.git) return window.git;
  if (typeof setStatus === 'function') setStatus('loading git engine…');
  // isomorphic-git's browser bundle needs a Buffer polyfill (not bundled).
  // jsdelivr's +esm build is a dependency-free ESM bundle we can import.
  if (typeof Buffer === 'undefined') {
    const mod = await import('https://cdn.jsdelivr.net/npm/buffer@6.0.3/+esm');
    window.Buffer = mod.Buffer;
  }
  // isomorphic-git's UMD detects AMD (monaco's loader defines window.define)
  // and would register as an AMD module instead of setting window.git.
  const hadDefine = Object.prototype.hasOwnProperty.call(window, 'define');
  const prevDefine = window.define;
  try { window.define = undefined; } catch (e) {}
  try {
    await loadScript('https://cdn.jsdelivr.net/npm/isomorphic-git@1.44.3/index.umd.min.js');
  } finally {
    if (hadDefine) { try { window.define = prevDefine; } catch (e) {} }
    else { try { delete window.define; } catch (e) {} }
  }
  if (typeof setStatus === 'function') setStatus('');
  if (!window.git) throw new Error('isomorphic-git failed to load (CDN).');
  return window.git;
}

function lineDiff(a, b) {
  // tiny LCS line diff for small files; returns [{t, s}]
  const A = a.split('\n'), B = b.split('\n');
  if (A.length * B.length > 4000000) {
    return [{ t: ' ', s: '(file too large for inline diff)' }];
  }
  const N = A.length, M = B.length;
  const dp = Array.from({ length: N + 1 }, () => new Uint16Array(M + 1));
  for (let i = N - 1; i >= 0; i--)
    for (let j = M - 1; j >= 0; j--)
      dp[i][j] = A[i] === B[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
  const out = [];
  let i = 0, j = 0;
  while (i < N && j < M) {
    if (A[i] === B[j]) { out.push({ t: ' ', s: A[i] }); i++; j++; }
    else if (dp[i + 1][j] >= dp[i][j + 1]) { out.push({ t: '-', s: A[i] }); i++; }
    else { out.push({ t: '+', s: B[j] }); j++; }
  }
  while (i < N) out.push({ t: '-', s: A[i++] });
  while (j < M) out.push({ t: '+', s: B[j++] });
  return out;
}

const GitUI = {
  inited: false,
  current: null, // selected file for diff

  async ensure() {
    await ensureGit();
    GitFS.syncFromVFS();
    try {
      await git.currentBranch({ fs: GitFS, dir: '/' });
      this.inited = true;
    } catch (e) {
      this.inited = false;
    }
    this.render();
  },

  statusLabel(row) {
    const [, head, workdir, stage] = row;
    if (workdir === 0 && head === 1) return ['deleted', 'm'];
    if (head === 0 && workdir === 2) return [stage === 2 ? 'added' : 'new', 'a'];
    if (workdir === 2 && head === 1 && stage !== 2) return ['modified', 'm'];
    if (stage === 2 || stage === 3) return ['staged', 'a'];
    return ['changed', 'm'];
  },

  async render() {
    const host = document.getElementById('gitPanel');
    if (!window.git) {
      host.innerHTML = '<p class="subtle">Git engine not loaded yet.</p><button class="small" id="gitLoad">Load git</button>';
      host.querySelector('#gitLoad').addEventListener('click', () => this.ensure());
      return;
    }
    host.innerHTML = `
      <p class="subtle" id="gitState">…</p>
      <div>
        <button class="small" id="gInit">Init repo</button>
        <button class="small ghost" id="gRefresh">Refresh</button>
        <button class="small ghost" id="gStageAll">Stage all</button>
      </div>
      <div class="pkg-row"><input class="sinput" id="gMsg" placeholder="commit message" style="margin:4px 0"><button class="small" id="gCommit">Commit</button></div>
      <div class="shead">CHANGES</div><div id="gChanges"></div>
      <div class="shead">DIFF <span class="subtle" id="gDiffFile"></span></div><pre class="diffview" id="gDiff">(select a changed file)</pre>
      <div class="shead">BRANCHES</div><div id="gBranches" style="font-size:12px"></div>
      <div class="pkg-row"><input class="sinput" id="gBranchName" placeholder="new branch"><button class="small ghost" id="gBranch">Create + switch</button></div>
      <div class="shead">LOG</div><pre class="log" id="gLog"></pre>
      <p class="subtle" style="margin-top:6px">Local git only — no remote, no push yet.</p>`;
    host.querySelector('#gInit').addEventListener('click', () => this.init());
    host.querySelector('#gRefresh').addEventListener('click', () => this.ensure());
    host.querySelector('#gStageAll').addEventListener('click', () => this.stageAll());
    host.querySelector('#gCommit').addEventListener('click', () =>
      this.commit(host.querySelector('#gMsg').value));
    host.querySelector('#gBranch').addEventListener('click', () =>
      this.createBranch(host.querySelector('#gBranchName').value.trim()));

    if (!this.inited) {
      host.querySelector('#gitState').textContent = 'Not a git repo yet — hit Init repo.';
      return;
    }
    await this.refreshState(host);
  },

  async init() {
    await ensureGit();
    GitFS.syncFromVFS();
    await git.init({ fs: GitFS, dir: '/', defaultBranch: 'main' });
    this.inited = true;
    this.render();
  },

  async refreshState(host) {
    host = host || document.getElementById('gitPanel');
    GitFS.syncFromVFS();
    const branch = await git.currentBranch({ fs: GitFS, dir: '/' }).catch(() => '(detached)');
    if (typeof StatusBar !== 'undefined') StatusBar.branch('git: ' + branch);
    const matrix = await git.statusMatrix({ fs: GitFS, dir: '/' });
    const changed = matrix.filter((r) => !(r[1] === 1 && r[2] === 1 && r[3] === 1));
    host.querySelector('#gitState').textContent =
      `on branch ${branch} — ${changed.length ? changed.length + ' change(s)' : 'clean'}`;

    const ch = host.querySelector('#gChanges');
    ch.innerHTML = '';
    if (!changed.length) ch.innerHTML = '<p class="subtle">No changes.</p>';
    changed.forEach((r) => {
      const [fp] = r;
      const [label, cls] = this.statusLabel(r);
      const div = document.createElement('div');
      div.className = 'st-row';
      div.innerHTML = `<span class="${cls}">[${label}]</span> <span></span> <button class="ghost small" style="float:right">stage</button>`;
      div.querySelector('span:nth-child(2)').textContent = fp;
      div.querySelector('button').addEventListener('click', async () => {
        await git.add({ fs: GitFS, dir: '/', filepath: fp });
        this.render();
      });
      div.addEventListener('click', (e) => {
        if (e.target.tagName !== 'BUTTON') this.showDiff(fp);
      });
      ch.appendChild(div);
    });

    // log
    const log = await git.log({ fs: GitFS, dir: '/', depth: 20 }).catch(() => []);
    host.querySelector('#gLog').textContent = log.length
      ? log.map((c) => {
          const cm = c.commit || {};
          const ts = cm.committer && cm.committer.timestamp
            ? new Date(cm.committer.timestamp * 1000).toLocaleString() : '';
          const msg = (cm.message || '').trim().split('\n')[0];
          return `${c.oid.slice(0, 7)}  ${ts}\n    ${msg}`;
        }).join('\n\n')
      : '(no commits yet)';

    // branches
    const branches = await git.listBranches({ fs: GitFS, dir: '/' }).catch(() => []);
    const bh = host.querySelector('#gBranches');
    bh.innerHTML = '';
    branches.forEach((b) => {
      const s = document.createElement('span');
      s.style.cssText = 'margin-right:8px;cursor:pointer' + (b === branch ? ';color:#3fb950;font-weight:700' : ';color:#79c0ff');
      s.textContent = (b === branch ? '● ' : '') + b;
      s.title = 'Switch to ' + b;
      s.addEventListener('click', () => this.checkout(b));
      bh.appendChild(s);
    });
  },

  async stageAll() {
    GitFS.syncFromVFS();
    const matrix = await git.statusMatrix({ fs: GitFS, dir: '/' });
    for (const [fp, head, workdir] of matrix) {
      if (workdir === 2) await git.add({ fs: GitFS, dir: '/', filepath: fp });
      else if (workdir === 0 && head === 1) await git.remove({ fs: GitFS, dir: '/', filepath: fp });
    }
    this.render();
  },

  async commit(message) {
    if (!message || !message.trim()) { print('Write a commit message first.', 'err'); return; }
    GitFS.syncFromVFS();
    try {
      const oid = await git.commit({
        fs: GitFS, dir: '/',
        message: message.trim(),
        author: { name: 'devworld-player', email: 'player@devworld.local' },
      });
      print(`Committed ${oid.slice(0, 7)} — ${message.trim()}`, 'ok');
    } catch (e) {
      print('Commit failed: ' + (e && e.message || e), 'err');
    }
    this.render();
  },

  async showDiff(fp) {
    const host = document.getElementById('gitPanel');
    const diffEl = host.querySelector('#gDiff');
    host.querySelector('#gDiffFile').textContent = '· ' + fp;
    GitFS.syncFromVFS();
    let headContent = '';
    try {
      const { blob } = await git.readBlob({ fs: GitFS, dir: '/', oid: 'HEAD', filepath: fp });
      headContent = td.decode(blob);
    } catch (e) { headContent = ''; } // new file
    const workContent = VFS.read(fp) || '';
    const parts = lineDiff(headContent, workContent);
    diffEl.innerHTML = parts.slice(0, 400).map((p) =>
      `<span class="${p.t === '-' ? 'del' : p.t === '+' ? 'add' : ''}">${escHtml(p.t + ' ' + p.s)}</span>`
    ).join('\n');
  },

  async createBranch(name) {
    if (!name) return;
    try {
      await git.branch({ fs: GitFS, dir: '/', ref: name, checkout: true });
      GitFS.syncToVFS();
      Explorer.render(); Tabs.render();
      if (typeof refreshEditorForPath === 'function') refreshEditorForPath();
      print(`Switched to new branch ${name}.`, 'ok');
    } catch (e) {
      print('Branch failed: ' + (e && e.message || e), 'err');
    }
    this.render();
  },

  async checkout(name) {
    try {
      await git.checkout({ fs: GitFS, dir: '/', ref: name });
      GitFS.syncToVFS();
      Explorer.render(); Tabs.render();
      if (typeof refreshEditorForPath === 'function') refreshEditorForPath();
      print(`Switched to branch ${name}.`, 'ok');
    } catch (e) {
      print('Checkout failed: ' + (e && e.message || e), 'err');
    }
    this.render();
  },
};

GitFS.load();
