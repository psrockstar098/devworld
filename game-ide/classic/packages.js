/* DEVWORLD IDE — packages panel.
   Honest about what's real:
   - Python: micropip.install into the Pyodide runtime (pure-Python wheels only).
   - JavaScript: an import-map helper. The map is stored per browser and can be
     copied into HTML pages you build (the Web page template). It does not
     affect `new Function` runs, which have no module loader. */

const IMPORTMAP_KEY = 'devworld.importmap.v1';

const PackagesUI = {
  map() {
    try { return JSON.parse(localStorage.getItem(IMPORTMAP_KEY) || '{}'); } catch (e) { return {}; }
  },
  saveMap(m) { try { localStorage.setItem(IMPORTMAP_KEY, JSON.stringify(m)); } catch (e) {} },

  render() {
    const host = document.getElementById('pkgPanel');
    host.innerHTML = '';

    // ---- Python ----
    const py = document.createElement('div');
    py.innerHTML = `<div class="shead">PYTHON (micropip)</div>
      <p class="subtle">Installs pure-Python wheels into the in-browser runtime. No compiled extensions.</p>
      <div class="pkg-row"><input class="sinput" id="pyPkg" placeholder="package name, e.g. numpy"><button class="small" id="pyInstall">Install</button></div>
      <div class="pkg-list" id="pyPkgs"></div>`;
    host.appendChild(py);
    py.querySelector('#pyInstall').addEventListener('click', () => this.installPython(py.querySelector('#pyPkg').value.trim()));

    // ---- JavaScript import map ----
    const js = document.createElement('div');
    js.innerHTML = `<div class="shead" style="margin-top:10px">JAVASCRIPT (import map)</div>
      <p class="subtle">Saved in this browser. Paste into an HTML page to use esm.sh modules:</p>
      <pre class="docsec" style="font-size:11px;background:#010409;border:1px solid #21262d;border-radius:6px;padding:8px;overflow:auto">&lt;script type="importmap"&gt;\n  { "imports": { ... } }\n&lt;/script&gt;\n&lt;script type="module"&gt;\n  import confetti from "canvas-confetti";\n&lt;/script&gt;</pre>
      <div class="pkg-row"><input class="sinput" id="imName" placeholder="name, e.g. canvas-confetti"><input class="sinput" id="imUrl" placeholder="url, e.g. https://esm.sh/canvas-confetti@1"></div>
      <div style="display:flex;gap:6px;margin:4px 0"><button class="small" id="imAdd">Add entry</button><button class="small ghost" id="imCopy">Copy JSON</button></div>
      <ul class="pkg-list" id="imList"></ul>`;
    host.appendChild(js);
    js.querySelector('#imAdd').addEventListener('click', () => {
      const n = js.querySelector('#imName').value.trim();
      const u = js.querySelector('#imUrl').value.trim();
      if (!n || !u) return;
      const m = this.map(); m[n] = u; this.saveMap(m);
      js.querySelector('#imName').value = ''; js.querySelector('#imUrl').value = '';
      this.render();
    });
    js.querySelector('#imCopy').addEventListener('click', () => {
      const json = JSON.stringify({ imports: this.map() }, null, 2);
      navigator.clipboard.writeText(json).then(
        () => print('Import map copied to clipboard.', 'ok'),
        () => print('Copy failed — select it manually.', 'err'));
    });
    const list = js.querySelector('#imList');
    Object.keys(this.map()).sort().forEach((n) => {
      const li = document.createElement('li');
      li.innerHTML = `<b></b>: <span class="subtle"></span> <button class="ghost small" style="float:right">remove</button>`;
      li.querySelector('b').textContent = n;
      li.querySelector('.subtle').textContent = this.map()[n];
      li.querySelector('button').addEventListener('click', () => {
        const m = this.map(); delete m[n]; this.saveMap(m); this.render();
      });
      list.appendChild(li);
    });
    if (!Object.keys(this.map()).length) list.innerHTML = '<li class="subtle">No entries yet.</li>';

    // ---- Server runtimes (approved dependencies) ----
    const sv = document.createElement('div');
    sv.innerHTML = `<div class="shead" style="margin-top:10px">SANDBOX RUNTIMES (approved dependencies)</div>
      <p class="subtle">Compiled/runtime-sandboxed languages have no network. Pick only from the packages the backend approves; versions are pinned.</p>
      <div class="pkg-list" id="srvList"></div>`;
    host.appendChild(sv);
    const srvList = sv.querySelector('#srvList');
    if (typeof backendCfg !== 'function' || !backendCfg()) {
      srvList.innerHTML = '<p class="subtle">Set the backend URL + API key in Settings to see approved packages.</p>';
    } else {
      this.loadServerDeps(srvList);
    }
  },

  async loadServerDeps(el) {
    const b = backendCfg();
    let approved = [];
    try {
      const res = await fetch(b.url.replace(/\/$/, '') + '/dependencies', { headers: backendHeaders(b) });
      const data = await res.json().catch(() => ({}));
      approved = Array.isArray(data.approved) ? data.approved : [];
    } catch (e) {
      el.innerHTML = '<p class="subtle">Backend unreachable: ' + ((e && e.message) || e) + '</p>';
      return;
    }
    const byRuntime = {};
    approved.forEach((d) => { (byRuntime[d.runtime] = byRuntime[d.runtime] || []).push(d); });
    const rids = Object.values(BACKEND_RUNTIME).filter((v, i, a) => a.indexOf(v) === i);
    el.innerHTML = '';
    for (const rid of rids) {
      const deps = byRuntime[rid] || [];
      const selected = window.DWDeps ? window.DWDeps.get(rid) : [];
      const label = (registry.find((l) => BACKEND_RUNTIME[l.id] === rid) || {}).name || rid;
      const row = document.createElement('div');
      row.className = 'pkg-row';
      row.style.justifyContent = 'space-between';
      row.innerHTML = `<span>${label}</span><span class="subtle">${deps.length ? deps.length + ' approved' : 'none'}</span>`;
      el.appendChild(row);
      for (const d of deps) {
        const wrap = document.createElement('label');
        wrap.className = 'pkg-row';
        wrap.style.gap = '6px';
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = selected.includes(d.name);
        cb.disabled = !d.baked;
        cb.addEventListener('change', () => {
          if (window.GameIDE) window.GameIDE.setBackendDependency(rid, d.name, cb.checked);
        });
        wrap.appendChild(cb);
        const span = document.createElement('span');
        span.textContent = `${d.name}@${d.version}` + (d.baked ? '' : ' — needs a package mirror');
        wrap.appendChild(span);
        el.appendChild(wrap);
      }
      if (!deps.length) {
        const none = document.createElement('p');
        none.className = 'subtle';
        none.textContent = 'No approved packages for this runtime yet.';
        el.appendChild(none);
      }
    }
  },

  async installPython(name) {
    if (!name) return;
    const listEl = document.getElementById('pyPkgs');
    try {
      const py = await ensurePyodide();
      setStatus(`installing ${name}…`);
      print(`Installing ${name} via micropip…`);
      await py.loadPackage('micropip');
      const micropip = py.pyimport('micropip');
      await micropip.install(name);
      setStatus('');
      print(`Installed ${name}.`, 'ok');
      if (listEl) {
        const li = document.createElement('li');
        li.textContent = name;
        listEl.appendChild(li);
      }
    } catch (e) {
      setStatus('');
      print(`Could not install ${name}: ${(e && e.message) || e}`, 'err');
      print('Note: only pure-Python wheels work in the browser build.', 'err');
    }
  },
};
