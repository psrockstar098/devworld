// shell.js — DEVWORLD game shell page logic.
// Owns: DOM panels/HUD, the IDE <iframe> overlay, audio bleeps, touch controls.
// The C++ (game.wasm) owns: world sim + rendering. Bridge via window.DW and
// Module.ccall into dw_ui_event / dw_mission_result / dw_hud_json / dw_touch.

window.DW = window.DW || {};

DW.ui = (() => {
  const panels = {};
  let hudData = null;

  function el(tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html != null) e.innerHTML = html;
    return e;
  }
  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"]/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'
    }[c]));
  }
  function send(type, extra) {
    const msg = Object.assign({ type }, extra || {});
    Module.ccall('dw_ui_event', null, ['string'], [JSON.stringify(msg)]);
  }

  function overlay(id) {
    let o = document.getElementById('ov-' + id);
    if (!o) {
      o = el('div', 'overlay');
      o.id = 'ov-' + id;
      o.innerHTML = '<div class="panel" id="panel-' + id + '"></div>';
      document.getElementById('ui-root').appendChild(o);
      o.addEventListener('click', (e) => {
        if (e.target === o && id !== 'charcreate') close(id);
      });
    }
    return o;
  }

  function open(id) {
    const o = overlay(id);
    o.classList.add('open');
  }
  function close(id) {
    if (id === 'all') {
      document.querySelectorAll('.overlay.open').forEach((o) => o.classList.remove('open'));
      DW.ide.close(true);
      send('panel_closed');
      return;
    }
    const o = document.getElementById('ov-' + id);
    if (o) o.classList.remove('open');
    if (id === 'idebar') DW.ide.close(true);
    send('panel_closed');
  }

  function notify(text, kind) {
    const box = document.getElementById('toasts');
    const t = el('div', 'toast ' + (kind || 'info'), esc(text));
    box.appendChild(t);
    setTimeout(() => t.classList.add('show'));
    setTimeout(() => { t.classList.remove('show'); setTimeout(() => t.remove(), 400); }, 4200);
  }

  function updateHud() {
    let json = '{}';
    try {
      const p = Module.ccall('dw_panel_json', 'number', ['string'], ['hudstate']);
      json = Module.UTF8ToString(p);
    } catch (e) { /* wasm not ready */ }
    let d = {};
    try { d = JSON.parse(json); } catch (e) {}
    hudData = d;
    const hud = document.getElementById('hud');
    if (!d.name) { hud.style.display = 'none'; return; }
    hud.style.display = 'flex';
    const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
    document.getElementById('hud-name').textContent = d.name;
    document.getElementById('hud-level').textContent = 'Lv ' + d.level;
    document.getElementById('hud-xpfill').style.width = pct + '%';
    document.getElementById('hud-xptext').textContent = d.xp + ' / ' + d.need + ' XP';
    document.getElementById('hud-credits').textContent = d.credits + ' cr';
    document.getElementById('hud-quests').textContent = 'Quests: ' + d.quests;
    document.getElementById('hud-clock').textContent = d.clock;
    document.getElementById('hud-map').textContent = d.map;
  }

  // ---- panel renderers ----
  const R = {
    charcreate(d) {
      const skins = [[232,190,150],[210,170,130],[160,110,80],[240,200,160]];
      const shirts = [[45,212,191],[200,80,80],[80,160,200],[200,170,60],[140,120,200]];
      const pants = [[51,65,85],[30,30,40],[110,80,55],[70,90,60]];
      const sw = (arr, key) => arr.map((c, i) =>
        `<button class="sw" data-k="${key}" data-i="${i}" style="background:rgb(${c.join(',')})"></button>`).join('');
      return `<h2>New Arrival</h2><p class="dim">The bus dropped you in Starter Town with a second-hand laptop and a head full of syntax. Who are you?</p>
        <label>Name <input id="cc-name" maxlength="16" value="Dev" /></label>
        <label>Display name <input id="cc-display" maxlength="16" value="Dev" /></label>
        <div class="swrow"><span>Skin</span>${sw(skins,'skin')}</div>
        <div class="swrow"><span>Shirt</span>${sw(shirts,'shirt')}</div>
        <div class="swrow"><span>Pants</span>${sw(pants,'pants')}</div>
        <button class="primary" id="cc-go">Start life in Starter Town</button>
        <script>void 0</script>`;
    },
    missionboard(d) {
      const stName = ['Locked','Available','Active','Submitted','Complete'];
      const rows = (d.missions || []).map((m) => {
        const st = stName[m.state] || '?';
        const btn = m.state === 1 ? `<button class="primary" data-act="accept" data-id="${esc(m.id)}">Accept</button>`
          : m.state === 2 ? `<span class="tag active">In progress — code it at a computer</span>`
          : m.state === 4 ? `<span class="tag done">Complete</span>`
          : `<span class="tag">Locked</span>`;
        return `<div class="mrow"><div><b>${esc(m.title)}</b> <span class="dim">${esc(m.language)} · ${m.tests} tests</span><br><span class="dim">${esc(m.briefing)}</span><br><span class="dim">Reward: ${m.xp} XP, ${m.credits} cr · from ${esc(m.giver)}</span></div><div>${btn}</div></div>`;
      }).join('');
      return `<h2>Mission Board</h2><p class="dim">Real code. Real tests. Hidden tests prove real skill.</p>${rows || '<p>No missions yet.</p>'}`;
    },
    quests(d) {
      const a = (d.active || []).map((m) => `<div class="mrow"><div><b>${esc(m.title)}</b><br><span class="dim">${esc(m.briefing)}</span></div></div>`).join('');
      const c = (d.complete || []).map((t) => `<div class="crow">✓ ${esc(t)}</div>`).join('');
      return `<h2>Quest Log</h2><h3>Active</h3>${a || '<p class="dim">Nothing active. The board is hiring.</p>'}<h3>Complete</h3>${c || '<p class="dim">—</p>'}`;
    },
    inventory(d) {
      const rows = (d.items || []).map((i) => `<div class="mrow"><div><b>${esc(i.name)}</b> ×${i.qty}<br><span class="dim">${esc(i.desc)}</span></div></div>`).join('');
      return `<h2>Inventory</h2>${rows || '<p class="dim">Empty pockets, full potential.</p>'}`;
    },
    shop(d) {
      const rows = (d.items || []).map((i) => `<div class="mrow"><div><b>${esc(i.name)}</b> — ${i.price} cr ${i.owned ? `<span class="tag">owned ×${i.owned}</span>` : ''}<br><span class="dim">${esc(i.desc)}</span></div><div><button data-act="buy" data-id="${esc(i.id)}" ${d.balance < i.price ? 'disabled' : ''}>Buy</button></div></div>`).join('');
      return `<h2>Market — Rafi's Wares</h2><p>Balance: <b>${d.balance} cr</b></p>${rows}`;
    },
    bank(d) {
      const rows = (d.rows || []).map((r) => `<div class="lrow"><span>${esc(r.ts.slice(0,10))}</span><span>${esc(r.memo)}</span><span class="${r.delta >= 0 ? 'pos' : 'neg'}">${r.delta >= 0 ? '+' : ''}${r.delta}</span></div>`).join('');
      return `<h2>Town Bank</h2><p>Balance: <b>${d.balance} cr</b> <span class="dim">(derived from ledger — every coin accounted for)</span></p><div class="ledger">${rows || '<p class="dim">No transactions yet.</p>'}</div>`;
    },
    worldmap(d) {
      const rows = (d.pois || []).map((p) => `<div class="mrow"><div><b>${esc(p.name)}</b></div></div>`).join('');
      return `<h2>Starter Town</h2><p class="dim">Walk the glowing paths. The bus south stays locked until you've proven yourself.</p>${rows}`;
    },
    skilltree(d) {
      const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
      return `<h2>Level ${d.level} — Developer</h2>
        <div class="xpbar big"><div class="xpfill" style="width:${pct}%"></div></div>
        <p>${d.xp} / ${d.need} XP · Reputation ${d.reputation}</p>
        <p class="dim">Skill trees track per-technology mastery through verified evidence. Phase 1: Python and SQL paths grow as you complete missions. Full trees arrive with the progression service (Phase 2).</p>`;
    },
    dialogue(d) {
      const opts = (d.options || []).map((o, i) => `<button class="opt" data-act="dopt" data-next="${esc(o.next)}" data-npc="${esc(d.npcId || '')}">${esc(o.text)}</button>`).join('');
      return `<h2>${esc(d.npc)}</h2><p class="dlg">${esc(d.text)}</p><div class="opts">${opts}</div>`;
    },
    settings() {
      return `<h2>Settings</h2>
        <h3>Controls</h3><p class="dim">Click a binding, then press a key. (Movement also works with arrow keys.)</p>
        <div id="binds"></div>
        <h3>Accessibility</h3>
        <label>UI scale <input type="range" id="uiscale" min="80" max="130" value="100" /></label>
        <p class="dim">Color-blind safe palette note: interactables always pulse teal; never color-only.</p>
        <h3>Safety</h3><p class="dim">Block / report arrive with multiplayer (Phase 2). Single-player: your world, your rules.</p>`;
    },
    pausemenu() {
      return `<h2>Paused</h2>
        <button class="opt" data-act="pclose">Resume</button>
        <button class="opt" data-act="pskill">Level &amp; skills</button>
        <button class="opt" data-act="psettings">Settings</button>
        <button class="opt" data-act="psave">Save now</button>
        <button class="opt" disabled>Friends (Phase 2)</button>
        <button class="opt" disabled>Company (Phase 2)</button>`;
    },
    idebar(d) {
      return `<div class="idebar-inner">
        <span class="ide-title">${d.title ? 'Mission: ' + esc(d.title) : 'Practice machine'}</span>
        ${d.mission ? `<button class="primary" id="ide-submit" data-mid="${esc(d.mission)}">Submit Solution</button>` : ''}
        <button id="ide-back">Back to Town (Esc)</button>
      </div>`;
    }
  };

  function show(id, data) {
    if (id === 'all') return;
    const o = overlay(id);
    const p = document.getElementById('panel-' + id);
    if (id === 'idebar') {
      // idebar is a top strip, not a modal
      document.getElementById('ide-bar').innerHTML = R.idebar(data || {});
      document.getElementById('ide-bar').style.display = 'block';
      wireIdeBar();
      return;
    }
    // dialogue needs the npc id carried for choices
    if (id === 'dialogue' && data) data.npcId = data.npcId || currentNpc;
    p.innerHTML = (R[id] ? R[id](data || {}) : '<p>?</p>') +
      (id === 'charcreate' ? '' : '<button class="closebtn" data-act="close">Close (Esc)</button>');
    if (id === 'charcreate') wireCharCreate(data);
    if (id === 'settings') wireSettings();
    wirePanel(p);
    open(id);
  }
  let currentNpc = '';
  function setNpc(id) { currentNpc = id; }

  function wirePanel(root) {
    root.querySelectorAll('[data-act]').forEach((b) => {
      b.addEventListener('click', () => {
        const a = b.getAttribute('data-act');
        if (a === 'close') close('all');
        else if (a === 'accept') send('mission_accept', { id: b.getAttribute('data-id') });
        else if (a === 'buy') send('shop_buy', { id: b.getAttribute('data-id') });
        else if (a === 'dopt') send('dialogue_choice', { npc: currentNpc, next: b.getAttribute('data-next') });
        else if (a === 'pclose') close('all');
        else if (a === 'pskill') { close('all'); requestPanel('skilltree'); }
        else if (a === 'psettings') { close('all'); show('settings', {}); }
        else if (a === 'psave') { notify('Progress saved.', 'success'); close('all'); }
      });
    });
  }
  function requestPanel(id) {
    try {
      const p = Module.ccall('dw_panel_json', 'number', ['string'], [id]);
      show(id, JSON.parse(Module.UTF8ToString(p)));
    } catch (e) { show(id, {}); }
  }
  function wireIdeBar() {
    const s = document.getElementById('ide-submit');
    if (s) s.addEventListener('click', () => {
      send('mission_submit', { id: s.getAttribute('data-mid') });
      notify('Submitting — the IDE is running your tests…', 'info');
    });
    const b = document.getElementById('ide-back');
    if (b) b.addEventListener('click', () => DW.ide.close());
  }

  const CC = { skin: 0, shirt: 0, pants: 0 };
  function wireCharCreate() {
    document.querySelectorAll('#panel-charcreate .sw').forEach((b) => {
      b.addEventListener('click', () => {
        CC[b.getAttribute('data-k')] = parseInt(b.getAttribute('data-i'));
        b.parentElement.querySelectorAll('.sw').forEach((x) => x.classList.remove('sel'));
        b.classList.add('sel');
      });
    });
    const skins = [[232,190,150],[210,170,130],[160,110,80],[240,200,160]];
    const shirts = [[45,212,191],[200,80,80],[80,160,200],[200,170,60],[140,120,200]];
    const pants = [[51,65,85],[30,30,40],[110,80,55],[70,90,60]];
    document.getElementById('cc-go').addEventListener('click', () => {
      send('char_done', {
        name: document.getElementById('cc-name').value || 'Dev',
        display: document.getElementById('cc-display').value || 'Dev',
        skin: skins[CC.skin], shirt: shirts[CC.shirt], pants: pants[CC.pants]
      });
    });
  }
  function wireSettings() {
    const acts = [['up','Move up'],['down','Move down'],['left','Move left'],['right','Move right'],['interact','Interact'],['map','World map'],['quests','Quest log'],['inventory','Inventory']];
    const box = document.getElementById('binds');
    box.innerHTML = acts.map((a) => `<button class="opt" data-bind="${a[0]}">${a[1]}: <span>press…</span></button>`).join('');
    box.querySelectorAll('[data-bind]').forEach((b) => {
      b.addEventListener('click', () => {
        b.querySelector('span').textContent = 'press a key…';
        const h = (e) => {
          e.preventDefault();
          Module.ccall('dw_set_keybind', null, ['string', 'number'], [b.getAttribute('data-bind'), e.keyCode]);
          b.querySelector('span').textContent = e.key;
          window.removeEventListener('keydown', h, true);
        };
        window.addEventListener('keydown', h, true);
      });
    });
    document.getElementById('uiscale').addEventListener('input', (e) => {
      document.documentElement.style.fontSize = (16 * e.target.value / 100) + 'px';
    });
  }

  return { show, hide: close, notify, updateHud, setNpc, esc };
})();

// ---- IDE overlay ----
DW.ide = (() => {
  let loaded = false, open = false;
  function frame() { return document.getElementById('ide-frame'); }
  function gameIDE() {
    try { return frame().contentWindow.GameIDE || null; } catch (e) { return null; }
  }
  function open_() {
    const ov = document.getElementById('ide-overlay');
    ov.style.display = 'flex';
    open = true;
    if (!loaded) {
      frame().src = 'ide/index.html';
      loaded = true;
      // The IDE boots async; flush queued mission seeds once GameIDE exists.
      const wait = setInterval(() => {
        try {
          if (frame().contentWindow.GameIDE) {
            clearInterval(wait);
            Module.ccall('dw_ide_ready', null, [], []);
          }
        } catch (e) {}
      }, 500);
      setTimeout(() => clearInterval(wait), 60000);
    }
    document.getElementById('hud').style.display = 'none';
  }
  function close(silent) {
    document.getElementById('ide-overlay').style.display = 'none';
    document.getElementById('ide-bar').style.display = 'none';
    open = false;
    document.getElementById('hud').style.display = 'flex';
    if (!silent) Module.ccall('dw_ui_event', null, ['string'], [JSON.stringify({ type: 'ide_closed' })]);
  }
  return { open: open_, close, isOpen: () => open, frame, gameIDE };
})();

DW.onIdeTestsDone = (r) => {
  Module.ccall('dw_mission_result', null, ['number', 'number'], [r.passed | 0, r.total | 0]);
};

// ---- audio: tiny WebAudio bleeps (ambient hooks, kept simple) ----
DW.audio = (() => {
  let ctx = null;
  function ac() {
    if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)();
    return ctx;
  }
  function tone(f, t0, dur, vol) {
    try {
      const c = ac(), o = c.createOscillator(), g = c.createGain();
      o.type = 'sine'; o.frequency.value = f;
      g.gain.setValueAtTime(vol || 0.08, c.currentTime + t0);
      g.gain.exponentialRampToValueAtTime(0.001, c.currentTime + t0 + dur);
      o.connect(g); g.connect(c.destination);
      o.start(c.currentTime + t0); o.stop(c.currentTime + t0 + dur);
    } catch (e) {}
  }
  const seq = {
    interact: [[660, 0, 0.07]], open: [[520, 0, 0.08], [780, 0.07, 0.1]],
    close: [[780, 0, 0.07], [520, 0.06, 0.08]], success: [[660, 0, 0.09], [880, 0.09, 0.12]],
    error: [[220, 0, 0.15]], coin: [[990, 0, 0.06], [1320, 0.06, 0.1]],
    levelup: [[523, 0, 0.1], [659, 0.1, 0.1], [784, 0.2, 0.16]]
  };
  function beep(name) { (seq[name] || seq.interact).forEach((n) => tone(n[0], n[1], n[2])); }
  return { beep };
})();

// ---- touch controls ----
(function () {
  if (!('ontouchstart' in window)) return;
  const pad = document.getElementById('touchpad');
  pad.style.display = 'block';
  pad.querySelectorAll('button').forEach((b) => {
    const d = b.getAttribute('data-d');
    const dn = (e) => { e.preventDefault(); Module.ccall('dw_touch', null, ['string', 'number'], [d, 1]); };
    const up = (e) => { e.preventDefault(); Module.ccall('dw_touch', null, ['string', 'number'], [d, 0]); };
    b.addEventListener('touchstart', dn, { passive: false });
    b.addEventListener('touchend', up, { passive: false });
    b.addEventListener('touchcancel', up, { passive: false });
  });
  document.getElementById('touch-e').addEventListener('touchstart', (e) => {
    e.preventDefault();
    // synthesize an E press through the keybind path is complex; use interact via ccall:
    // (the game also shows prompts; E is the interact key)
    window.dispatchEvent(new KeyboardEvent('keydown', { keyCode: 69 }));
  }, { passive: false });
})();
