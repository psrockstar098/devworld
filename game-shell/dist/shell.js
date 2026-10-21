// shell.js — DEVWORLD game shell page logic.
// Owns: DOM panels/HUD, the IDE <iframe> overlay, audio bleeps, touch controls.
// The C++ (game.wasm) owns: world sim + rendering. Bridge via window.DW and
// Module.ccall into dw_ui_event / dw_mission_result / dw_hud_json / dw_touch.

window.DW = window.DW || {};

// Tell the C++ world that a page-owned device/panel now owns input.
function cppEvent(type) {
  try {
    if (window.Module && Module.ccall) {
      Module.ccall('dw_ui_event', null, ['string'], [JSON.stringify({ type })]);
    }
  } catch (e) {}
}

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

  function applyA11y(d) {
    const root = document.documentElement;
    root.style.fontSize = (16 * (d.uiScale || 100) / 100) + 'px';
    root.style.setProperty('--text-scale', String((d.textScale || 100) / 100));
    root.classList.toggle('a11y-contrast', !!d.highContrast);
    root.classList.toggle('a11y-colorblind', !!d.colorblind);
    root.classList.toggle('reduced-motion', !!d.reducedMotion);
  }

  // Tiny pixel-art portrait renderer for the HUD and character creation.
  function paintAvatar(canvas, colors, opts) {
    if (!canvas) return;
    opts = opts || {};
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.imageSmoothingEnabled = false;
    const col = (v, fallback) => Array.isArray(v) && v.length >= 3 ? v : fallback;
    const skin = col(colors && colors.skin, [232, 190, 150]);
    const hair = col(colors && colors.hair, [45, 35, 28]);
    const shirt = col(colors && colors.shirt, [45, 212, 191]);
    const pants = col(colors && colors.pants, [51, 65, 85]);
    const rgb = (c) => `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`;
    const shade = (c, d) => [c[0] + d, c[1] + d, c[2] + d].map((v) => Math.max(0, Math.min(255, v)));
    const full = !!opts.full;
    const baseW = full ? 34 : 24;
    const baseH = full ? 44 : 24;
    const s = Math.max(1, Math.floor(Math.min(canvas.width / baseW, canvas.height / baseH)));
    const ox = Math.floor((canvas.width - baseW * s) / 2);
    const oy = Math.floor((canvas.height - baseH * s) / 2);
    const r = (x, y, w, h, c) => { ctx.fillStyle = rgb(c); ctx.fillRect(ox + x * s, oy + y * s, w * s, h * s); };

    if (!full) {
      // Bust: head, shoulders, a hint of hair.
      r(7, 3, 10, 3, hair);
      r(6, 5, 12, 2, hair);
      r(7, 7, 10, 8, skin);
      r(6, 8, 1, 5, shade(skin, -18));
      r(7, 7, 10, 2, shade(skin, 8));
      r(7, 14, 10, 4, skin);
      r(4, 16, 16, 6, shirt);
      r(4, 16, 16, 2, shade(shirt, 16));
      r(7, 18, 10, 4, shade(shirt, -12));
      r(9, 18, 6, 2, [238, 232, 218]);
      // Eyes.
      r(9, 10, 2, 2, [30, 34, 42]);
      r(13, 10, 2, 2, [30, 34, 42]);
      r(10, 13, 4, 1, shade(skin, -58));
      // Hair sideburns.
      r(6, 9, 1, 4, shade(hair, -16));
      r(17, 9, 1, 4, shade(hair, -16));
      return;
    }

    // Full body, feet at baseH - 4.
    r(9, 38, 16, 2, [8, 12, 16]);
    r(10, 26, 5, 11, pants);
    r(19, 26, 5, 11, pants);
    r(9, 36, 6, 3, [42, 38, 38]);
    r(19, 36, 6, 3, [42, 38, 38]);
    r(8, 14, 18, 14, [22, 26, 34]);
    r(9, 15, 16, 12, shirt);
    r(10, 15, 3, 11, shade(shirt, 18));
    r(22, 16, 4, 11, shade(shirt, -26));
    r(9, 15, 16, 2, [238, 232, 218]);
    r(14, 15, 6, 2, shade(shirt, -40));
    r(6, 17, 4, 10, shirt);
    r(24, 17, 4, 10, shirt);
    r(6, 25, 4, 4, skin);
    r(24, 25, 4, 4, skin);
    r(10, 4, 14, 12, [22, 26, 34]);
    r(11, 5, 12, 10, skin);
    r(9, 8, 2, 4, shade(skin, -16));
    r(23, 8, 2, 4, shade(skin, -16));
    r(9, 1, 16, 6, hair);
    r(10, 2, 14, 2, shade(hair, 18));
    r(9, 6, 2, 6, hair);
    r(23, 6, 2, 6, hair);
    r(12, 9, 2, 2, [30, 34, 42]);
    r(18, 9, 2, 2, [30, 34, 42]);
    r(15, 12, 2, 1, shade(skin, -60));
    // Teal player chevron.
    r(14, -4, 6, 2, [59, 215, 190]);
    r(15, -2, 4, 2, [59, 215, 190]);
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
    applyA11y(d);
    const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
    document.getElementById('hud-name').textContent = d.name;
    document.getElementById('hud-level').textContent = 'Lv ' + d.level;
    document.getElementById('hud-xpfill').style.width = pct + '%';
    document.getElementById('hud-xptext').textContent = d.xp + ' / ' + d.need + ' XP';
    document.getElementById('hud-credits').textContent = d.credits + ' cr';
    document.getElementById('hud-quests').textContent = d.quests;
    document.getElementById('hud-clock').textContent = d.clock;
    document.getElementById('hud-map').textContent = d.map;
    const title = document.getElementById('hud-title');
    if (title) title.textContent = d.title || '';
    const rep = document.getElementById('hud-rep');
    if (rep) rep.textContent = d.reputationTier ? `${d.reputationTier} · ${d.reputation} rep` : '';
    const daypart = document.getElementById('hud-daypart');
    if (daypart) daypart.textContent = d.daypart || 'Dusk';
    paintAvatar(document.getElementById('hud-avatar'), d, { full: false });
    const card = document.getElementById('hud-track-card');
    const track = document.getElementById('hud-track');
    if (card && track) {
      const has = !!d.trackedTitle;
      card.style.display = has ? 'flex' : 'none';
      track.textContent = has
        ? `${d.trackedTitle} — ${d.trackedObjective || 'in progress'}`
        : '';
    }
  }

  // ---- shared town views (also painted incrementally while the panel is open) ----
  function rgbHex(c) {
    const a = Array.isArray(c) ? c : [90, 110, 120];
    return '#' + a.slice(0, 3).map((v) => {
      const n = Math.max(0, Math.min(255, Number(v) || 0));
      return n.toString(16).padStart(2, '0');
    }).join('');
  }

  function townOnlineHtml(d) {
    const me = d.me || {};
    const friends = new Set((d.friends || []).map((f) => f.id));
    const incoming = new Set((d.incoming || []).map((f) => f.id));
    const outgoing = new Set((d.outgoing || []).map((f) => f.id));
    const rows = (d.online || []).filter((p) => p && p.id !== me.id).map((p) => {
      let action = `<button data-act="town_add" data-id="${esc(p.id)}">Add friend</button>`;
      if (friends.has(p.id)) action = '<span class="tag done">friend</span>';
      else if (incoming.has(p.id)) action = `<button data-act="town_accept" data-id="${esc(p.id)}">Accept</button><button data-act="town_decline" data-id="${esc(p.id)}">Decline</button>`;
      else if (outgoing.has(p.id)) action = '<span class="tag">requested</span>';
      return `<div class="mrow town-row"><div><b>${esc(p.displayName || p.handle || 'Player')}</b>
        <span class="dim">@${esc(p.handle || '')}</span><br>
        <span class="dim">Home Row #${esc(String(p.homeUnit || '—'))}${p.mapId && p.mapId !== 'town' ? ' · inside' : ''}</span></div>
        <div class="town-acts">${action}</div></div>`;
    }).join('');
    return rows || '<p class="dim">Nobody else is in town right now. The plaza is yours.</p>';
  }

  function townChatHtml(d) {
    const me = d.me || {};
    const rows = (d.chat || []).slice(-60).map((m) => {
      const from = (m.from && (m.from.displayName || m.from.handle)) || 'Someone';
      const mine = m.from && me.id && m.from.id === me.id;
      const when = String(m.at || '').slice(11, 16);
      return `<div class="msg ${mine ? 'mine' : 'theirs'}"><span><b>${esc(from)}</b> ${esc(m.text || '')}</span><span class="dim">${esc(when)}</span></div>`;
    }).join('');
    return rows || '<p class="dim">No messages yet. Say hello to the town.</p>';
  }

  function townFriendsHtml(d) {
    const block = (title, list, kind) => {
      if (!list || !list.length) return '';
      const rows = list.map((f) => {
        const status = f.online ? '<span class="tag done">online</span>' : '<span class="tag">offline</span>';
        let btns = '';
        if (kind === 'incoming') {
          btns = `<button class="primary" data-act="town_accept" data-id="${esc(f.id)}">Accept</button><button data-act="town_decline" data-id="${esc(f.id)}">Decline</button>`;
        } else if (kind === 'friends') {
          btns = `<button data-act="town_remove" data-id="${esc(f.id)}">Remove</button>`;
        }
        return `<div class="mrow town-row"><div><b>${esc(f.displayName || f.handle || 'Player')}</b> <span class="dim">@${esc(f.handle || '')}</span> ${status}</div><div class="town-acts">${btns}</div></div>`;
      }).join('');
      return `<h3>${esc(title)}</h3>${rows}`;
    };
    return block('Friends', d.friends, 'friends') +
      block('Friend requests', d.incoming, 'incoming') +
      block('Sent requests', d.outgoing, 'outgoing') ||
      '<p class="dim">No friends yet. Add someone from the online list.</p>';
  }

  function colorField(id, label, color) {
    return `<label class="color-field">${esc(label)} <input type="color" id="app-${esc(id)}" value="${rgbHex(color)}" /></label>`;
  }

  function clockNow() {
    const d = new Date();
    return String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
  }

  // ---- panel renderers ----
  const R = {
    charcreate(d) {
      const skins = [[232,190,150],[210,170,130],[160,110,80],[240,200,160]];
      const shirts = [[45,212,191],[200,80,80],[80,160,200],[200,170,60],[140,120,200]];
      const pants = [[51,65,85],[30,30,40],[110,80,55],[70,90,60]];
      const hairc = [[45,35,28],[90,60,40],[180,140,90],[30,30,34],[150,150,150],[160,60,70]];
      const sw = (arr, key) => arr.map((c, i) =>
        `<button class="sw${i === 0 ? ' sel' : ''}" data-k="${key}" data-i="${i}" title="${key} ${i + 1}" style="background:rgb(${c.join(',')})"></button>`).join('');
      return `<h2>New Arrival</h2>
        <div class="cc-grid">
          <div class="cc-preview">
            <canvas id="cc-avatar" width="120" height="150"></canvas>
            <div class="cc-name" id="cc-preview-name">Dev</div>
            <div class="cc-cap">Starter Town · 2026</div>
          </div>
          <div class="cc-form">
            <p class="dim">The bus dropped you in Starter Town with a second-hand laptop and a head full of syntax. The town is hiring. Pick who walks off the platform.</p>
            <label>Name <input id="cc-name" maxlength="16" value="Dev" /></label>
            <label>Display name <input id="cc-display" maxlength="16" value="Dev" /></label>
            <div class="swrow"><span>Skin</span>${sw(skins,'skin')}</div>
            <div class="swrow"><span>Hair</span>${sw(hairc,'hair')}</div>
            <div class="swrow"><span>Shirt</span>${sw(shirts,'shirt')}</div>
            <div class="swrow"><span>Pants</span>${sw(pants,'pants')}</div>
            <p class="dim">Fine-tune this later from the Town panel; your look is saved to your account.</p>
            <button class="primary" id="cc-go">Start life in Starter Town</button>
          </div>
        </div>`;
    },
    missionboard(d) {
      const stName = ['Locked','Available','Active','Submitted','Complete'];
      const rows = (d.missions || []).map((m) => {
        const st = stName[m.state] || '?';
        const gate = m.gate ? `<span class="tag gate">GATE · no hints</span>` : '';
        const btn = m.state === 1 ? `<button class="primary" data-act="accept" data-id="${esc(m.id)}">Accept</button>`
          : m.state === 2 ? `<span class="tag active">In progress — code it at a computer</span>`
          : m.state === 4 ? `<span class="tag done">Complete</span> <button data-act="practice" data-id="${esc(m.id)}">${m.practicing ? 'Practising…' : 'Practice'}</button>`
          : `<span class="tag">Locked</span>`;
        return `<div class="mrow"><div><b>${esc(m.title)}</b> ${gate} <span class="dim">L${m.level} · ${esc(m.language)} · ${m.tests} tests</span><br><span class="dim">${esc(m.briefing)}</span><br><span class="dim">Reward: ${m.xp} XP, ${m.credits} cr · from ${esc(m.giver)}</span></div><div>${btn}</div></div>`;
      }).join('');
      return `<h2>Mission Board</h2><p class="dim">Real code. Real tests. Hidden tests prove real skill.</p>${rows || '<p>No missions yet.</p>'}`;
    },
    quests(d) {
      const a = (d.active || []).map((m) => {
        const gate = m.gate ? '<span class="tag gate">GATE</span>' : '';
        const track = m.tracked
          ? `<span class="tag active">tracked</span>`
          : `<button data-act="track" data-id="${esc(m.id)}">Track</button>`;
        return `<div class="mrow"><div><b>${esc(m.title)}</b> ${gate} <span class="dim">L${m.level}</span><br><span class="dim">${esc(m.objective || m.briefing)}</span></div><div>${track}</div></div>`;
      }).join('');
      const c = (d.complete || []).map((m) => `<div class="crow">✓ ${esc(m.title)}</div>`).join('');
      const tracked = d.tracked ? `<p class="dim">Tracking an objective — it shows in the HUD.</p>` : '';
      return `<h2>Quest Log</h2>${tracked}<h3>Active</h3>${a || '<p class="dim">Nothing active. The board is hiring.</p>'}<h3>Complete</h3>${c || '<p class="dim">—</p>'}`;
    },
    inventory(d) {
      const slots = (d.slots || []).map((s) => {
        const body = s.empty
          ? `<span class="dim">empty</span>`
          : `<b>${esc(s.name)}</b>`;
        const un = s.empty ? '' : `<button data-act="unequip" data-slot="${esc(s.slot)}">Unequip</button>`;
        return `<div class="mrow"><div><span class="tag">${esc(s.slot)}</span> ${body}</div><div>${un}</div></div>`;
      }).join('');
      const rows = (d.items || []).map((i) => {
        const eq = i.equipped ? `<span class="tag done">equipped</span>` : '';
        const btn = i.equippable
          ? `<button data-act="equip" data-id="${esc(i.id)}" ${i.equipped ? 'disabled' : ''}>Equip</button>`
          : '';
        return `<div class="mrow"><div><b>${esc(i.name)}</b> ×${i.qty} ${eq}<br><span class="dim">${esc(i.desc)}</span><br><span class="dim">${esc(i.slot)} · ${esc(i.effect)}</span></div><div>${btn}</div></div>`;
      }).join('');
      const t = d.totals || {};
      return `<h2>Inventory</h2>
        <h3>Load-out</h3>${slots}
        <p class="dim">Bonuses: +${t.xpBonus || 0}% mission XP · ${t.missionSlots || 1} mission slot(s) · ${t.buildSpeed || 0}% build speed · ${t.comfort || 0} comfort</p>
        <h3>Carried</h3>${rows || '<p class="dim">Empty pockets, full potential.</p>'}`;
    },
    shop(d) {
      const bias = d.bias ? ` <span class="tag">market ${d.bias > 0 ? '+' : ''}${d.bias}%</span>` : '';
      const rows = (d.items || []).map((i) => `<div class="mrow"><div><b>${esc(i.name)}</b> — ${i.price} cr ${i.base && i.base !== i.price ? `<span class="dim">(base ${i.base})</span>` : ''} ${i.owned ? `<span class="tag">owned ×${i.owned}</span>` : ''}<br><span class="dim">${esc(i.desc)}</span></div><div><button data-act="buy" data-id="${esc(i.id)}" ${d.balance < i.price ? 'disabled' : ''}>Buy</button></div></div>`).join('');
      return `<h2>Market — Rafi's Wares</h2><p>Balance: <b>${d.balance} cr</b>${d.community ? ` · ${esc(d.community)}${bias}` : ''}</p>${rows}`;
    },
    bank(d) {
      const rows = (d.rows || []).map((r) => `<div class="lrow"><span>${esc(r.ts.slice(0,10))}</span><span>${esc(r.memo)}</span><span class="${r.delta >= 0 ? 'pos' : 'neg'}">${r.delta >= 0 ? '+' : ''}${r.delta}</span></div>`).join('');
      return `<h2>Town Bank</h2><p>Balance: <b>${d.balance} cr</b> <span class="dim">(derived from ledger — every coin accounted for)</span></p><div class="ledger">${rows || '<p class="dim">No transactions yet.</p>'}</div>`;
    },
    worldmap(d) {
      const pois = (d.pois || []).map((p) =>
        `<div class="mrow"><div><b>${esc(p.name)}</b> <span class="dim">${esc(p.kind)} · ${p.x},${p.y}</span></div></div>`).join('');
      const eco = d.economy ? `<p class="dim">Community <b>${esc(d.economy.community)}</b> · jobs: ${esc(d.economy.jobFocus)} · living cost ${d.economy.livingCost} cr/day · technology market ${d.economy.marketBias > 0 ? '+' : ''}${d.economy.marketBias}%</p>` : '';
      const dungeons = (d.dungeons || []).map((x) => {
        const tag = x.cleared ? '<span class="tag done">cleared</span>'
          : x.enterable ? '<span class="tag active">enterable</span>'
          : `<span class="tag">${esc(x.reason || 'sealed')}</span>`;
        return `<div class="mrow"><div><b>${esc(x.name)}</b> ${tag} <span class="dim">L${x.minLevel}+</span><br><span class="dim">${esc(x.blurb)}</span></div></div>`;
      }).join('');
      const cities = (d.cities || []).map((c) => {
        const tag = c.here ? '<span class="tag active">you are here</span>'
          : c.unlocked ? '<span class="tag done">bus route open</span>'
          : `<span class="tag">locked · L${c.level}+</span>`;
        return `<div class="mrow"><div><b>${esc(c.name)}</b> ${tag}<br><span class="dim">${esc(c.blurb)}</span><br><span class="dim">${esc(c.community || '')} · ${esc(c.jobFocus || '')} · rent ${c.livingCost || 0} cr/day · market ${c.marketBias > 0 ? '+' : ''}${c.marketBias || 0}%</span></div></div>`;
      }).join('');
      return `<h2>${esc(d.city || 'World')}</h2>
        <p class="dim">Region <b>${esc(d.region || '?')}</b> · instance <b>${esc(d.instance || 'shared')}</b> · map <b>${esc(d.map || '')}</b></p>
        ${eco}
        <h3>Hidden technology</h3>${dungeons || '<p class="dim">Rumours only, so far. Talk to people.</p>'}
        <h3>Around you</h3>${pois || '<p class="dim">Nothing marked here.</p>'}
        <h3>City directory</h3>${cities}
        <p class="dim">Fast travel happens at any bus stop — walk there and press E.</p>`;
    },
    fasttravel(d) {
      const rows = (d.offers || []).map((o) => {
        const btn = o.unlocked
          ? `<button class="primary" data-act="travel" data-id="${esc(o.id)}" ${d.balance < o.fare ? 'disabled' : ''}>Ride — ${o.fare} cr</button>`
          : `<span class="tag">${esc(o.reason)}</span>`;
        return `<div class="mrow"><div><b>${esc(o.name)}</b> <span class="dim">${esc(o.region)}</span><br><span class="dim">${esc(o.blurb)}</span><br><span class="dim">Fare: ${o.fare} cr</span></div><div>${btn}</div></div>`;
      }).join('');
      return `<h2>Bus — ${esc(d.city || '')}</h2><p>Balance: <b>${d.balance} cr</b></p>${rows || '<p class="dim">No routes from here yet.</p>'}`;
    },
    skilltree(d) {
      const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
      const areas = (d.areas || []).map((a) => {
        const topics = (a.topics || []).map((t) => {
          const stateClass = t.state === 'mastered' ? 'tag done' : 'tag';
          const tpct = Math.round((t.score || 0) * 100);
          const bits = [`d${t.difficulty}`];
          if (t.evidence) bits.push(`${t.evidence} evidence`);
          if (t.independent) bits.push('independent');
          if (t.due) bits.push('review due');
          if (t.weakest && t.state !== 'mastered' && t.state !== 'locked') bits.push(`weakest: ${esc(t.weakest).replace(/_/g, ' ')}`);
          const req = (t.requires && t.requires.length && t.state === 'locked')
            ? `<div class="dim">Needs: ${t.requires.map(esc).join(', ')}</div>` : '';
          const obj = (t.objectives || []).length
            ? `<div class="dim">${t.objectives.map(esc).join(' · ')}</div>` : '';
          return `<div class="mrow"><div><b>${esc(t.title)}</b>
            <div class="dim">${bits.map(esc).join(' · ')}</div>
            <div class="xpbar"><div class="xpfill" style="width:${tpct}%"></div></div>
            ${req}${obj}</div>
            <div><span class="${stateClass}">${esc(String(t.state).replace('_', ' '))}</span></div></div>`;
        }).join('');
        const open = (a.topics || []).some((t) => t.state !== 'locked' && t.state !== 'available');
        return `<details${open ? ' open' : ''}><summary>${esc(a.title)} <span class="dim">— ${esc(a.summary)}</span></summary>${topics}</details>`;
      }).join('');
      const practice = (d.practice || []).map((p) => `<span class="tag">${esc(p.title)} · ${Math.round((p.score || 0) * 100)}%</span>`).join(' ');
      return `<h2>Level ${d.level} — ${esc(d.title || 'Developer')}</h2>
        <div class="xpbar big"><div class="xpfill" style="width:${pct}%"></div></div>
        <p>${d.xp} / ${d.need} XP · Reputation ${d.reputation} · <b>${d.mastered || 0}/${d.topics || 0}</b> topics mastered${d.dueCount ? ` · <b>${d.dueCount}</b> review(s) due` : ''}</p>
        <p class="dim">Mastery is earned from evidence, never from watching: only an independent challenge (a gate or a proof) produces evidence strong enough to master a skill. Debugging your own failed attempts counts as evidence too.</p>
        ${practice ? `<p class="dim">Practise next: ${practice}</p>` : ''}
        ${areas}`;
    },
    progression(d) {
      const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
      const langs = (d.languages || []).map((l) =>
        `<span class="tag done">${esc(l.language)} · ${esc(l.label)}</span>`).join(' ');
      const career = d.career || {};
      const paths = (career.paths || []).map((p) => {
        const btn = p.current
          ? '<span class="tag done">current path</span>'
          : p.eligible
            ? `<button class="primary" data-act="choose_career" data-id="${esc(p.id)}">${career.path ? 'Switch to' : 'Choose'}</button>`
            : '<span class="tag">locked</span>';
        const why = (!p.eligible && (p.missing || []).length)
          ? `<div class="dim">Needs: ${p.missing.map(esc).join('; ')}</div>` : '';
        return `<div class="mrow"><div><b>${esc(p.title)}</b> <span class="dim">${esc(p.language)}</span><br><span class="dim">${esc(p.blurb)}</span>${why}</div><div>${btn}</div></div>`;
      }).join('');
      const history = (career.history || []).map((h) => `<span class="tag">${esc(h)}</span>`).join(' ');
      const ach = (d.achievements || []).map((a) => a.earned
        ? `<div class="mrow"><div><b>${esc(a.title)}</b><br><span class="dim">${esc(a.desc)}</span></div><span class="tag done">earned</span></div>`
        : `<div class="mrow"><div><b>${esc(a.title)}</b><br><span class="dim">${esc(a.desc)}</span></div><span class="tag">locked</span></div>`
      ).join('');
      const unlocks = (d.unlocks || []).map((u) => {
        const why = u.unlocked
          ? `<div class="dim">Why: ${(u.met || []).map(esc).join('; ')}</div>`
          : `<div class="dim">Needs: ${(u.missing || []).map(esc).join('; ')}</div>`;
        const tag = u.unlocked ? '<span class="tag done">open</span>' : '<span class="tag">locked</span>';
        return `<div class="mrow"><div><b>${esc(u.title)}</b> <span class="dim">${esc(String(u.kind).replace(/_/g, ' '))}</span><br><span class="dim">${esc(u.blurb)}</span>${why}</div><div>${tag}</div></div>`;
      }).join('');
      return `<h2>Progression — Level ${d.level} ${esc(d.title || 'Developer')}</h2>
        <div class="xpbar big"><div class="xpfill" style="width:${pct}%"></div></div>
        <p>${d.xp} / ${d.need} XP · Reliability <b>${d.reliability}</b> (${esc(d.reliabilityTier)}) · Reputation ${d.reputation}</p>
        <p class="dim">${d.mastered} topic(s) mastered · ${d.independentSkills} independent skill(s) · ${d.independentCount} independent assessment(s) · ${d.gates} gate(s) passed${d.companyRep ? ` · company reputation ${d.companyRep}` : ''}</p>
        <h3>Languages</h3><p>${langs || '<span class="dim">No completed work yet.</span>'}</p>
        <h3>Career</h3>
        <p class="dim">Switching paths never costs you what you have earned — every competency stays.</p>
        ${paths || '<p class="dim">No paths authored.</p>'}
        ${history ? `<p class="dim">Paths held: ${history}</p>` : ''}
        <h3>Achievements (${d.earnedCount || 0}/${d.achievementTotal || 0})</h3>
        ${ach || '<p class="dim">none yet</p>'}
        <h3>Opportunities &amp; unlocks</h3>
        <p class="dim">City access turns on breadth, company eligibility on leadership and reliability, and a technical privilege only ever names its own field — so you never have to master an unrelated technology to reach a career that fits.</p>
        ${unlocks || '<p class="dim">No unlocks authored.</p>'}`;
    },
    dialogue(d) {
      const opts = (d.options || []).map((o, i) => `<button class="opt" data-act="dopt" data-next="${esc(o.next)}" data-sets="${esc(o.sets || '')}" data-npc="${esc(d.npcId || '')}">${esc(o.text)}</button>`).join('');
      return `<h2>${esc(d.npc)}</h2><p class="dlg">${esc(d.text)}</p><div class="opts">${opts}</div>`;
    },
    profile(d) {
      const pct = d.need > 0 ? Math.min(100, Math.round((d.xp / d.need) * 100)) : 0;
      const visits = (d.visited || []).map((c) => `<span class="tag done">${esc(c)}</span>`).join(' ');
      const ach = (d.achievements || []).map((a) => `<span class="tag">${esc(a)}</span>`).join(' ');
      const loadout = (d.loadout || []).map((l) => {
        const [slot, item] = String(l).split(':');
        return `<div class="mrow"><div><span class="tag">${esc(slot)}</span> ${esc(item)}</div></div>`;
      }).join('');
      return `<h2>${esc(d.displayName)} <span class="dim">(${esc(d.name)})</span></h2>
        <p class="dim">${esc(d.title)} · Level ${d.level} · <b>${esc(d.reputationTier)}</b> (${d.reputation} rep)</p>
        <div class="xpbar big"><div class="xpfill" style="width:${pct}%"></div></div>
        <p>${d.xp} / ${d.need} XP
           · ${d.missionsComplete} missions complete · ${d.missionsActive} active
           · ${d.missionSlots} mission slot(s) · ${d.playMinutes} min played</p>
        <h3>Load-out</h3>${loadout || '<p class="dim">Nothing equipped.</p>'}
        <p class="dim">Gear bonuses: +${d.xpBonus}% mission XP · ${d.buildSpeed}% build speed · ${d.comfort} comfort (convenience only)</p>
        <h3>Proof of Skill</h3><p>${ach || '<span class="dim">none yet</span>'}</p>
        <h3>Cities visited</h3><p>${visits || '<span class="dim">just Starter Town so far</span>'}</p>`;
    },
    notifications(d) {
      const rows = (d.items || []).map((n) => {
        const when = (n.at || '').replace('T', ' ').slice(5, 16);
        return `<div class="mrow"><div><span class="tag ${esc(n.type)}">${esc(n.type)}</span> ${esc(n.text)}</div><div class="dim">${esc(when)}</div></div>`;
      }).join('');
      return `<h2>Notifications</h2>
        <p class="dim">Everything the world told you, newest first.</p>
        ${rows || '<p class="dim">No notifications yet.</p>'}
        <button data-act="notifications_clear">Clear log</button>`;
    },
    examine(d) {
      return `<h2>${esc(d.title || 'Look closer')}</h2><p class="dlg">${esc(d.text || 'Nothing remarkable.')}</p>`;
    },
    portfolio(d) {
      const rows = (d.entries || []).map((e) => {
        const tag = e.completed ? '<span class="tag done">shipped</span>' : '<span class="tag active">in progress</span>';
        return `<div class="mrow"><div><b>${esc(e.title)}</b> ${tag} ${e.gate ? '<span class="tag gate">GATE</span>' : ''}<br><span class="dim">${esc(e.language)} · L${e.level} · ${e.tests} tests · ${e.xp} XP</span></div></div>`;
      }).join('');
      return `<h2>Project Portfolio</h2>
        <p>Evidence score <b>${d.evidence}</b> — <b>${esc(d.tier)}</b></p>
        <p class="dim">${d.completed} project(s) shipped · reputation ${d.reputation} (${esc(d.reputationTier)})</p>
        <p class="dim">Evidence = completed projects + hidden tests passed. Cosmetic gear cannot change it.</p>
        ${rows || '<p class="dim">Nothing shipped yet. The board is hiring.</p>'}`;
    },
    company(d) {
      if (!d.founded) {
        const opts = (d.focuses || []).map((f) => `<option value="${esc(f)}">${esc(f)}</option>`).join('');
        return `<h2>Found a Company</h2>
          <p class="dim">A company is convenience: dashboard, roster and slightly better contract pay. It never changes a test verdict.</p>
          <label>Name <input id="co-name" maxlength="24" placeholder="Blue Pixel" /></label>
          <label>Focus <select id="co-focus">${opts}</select></label>
          <button class="primary" data-act="company_found">Found company</button>`;
      }
      const roster = (d.roster || []).map((c) => {
        const btn = c.hired
          ? `<button data-act="unhire" data-id="${esc(c.id)}">Let go</button>`
          : `<button data-act="hire" data-id="${esc(c.id)}">Hire</button>`;
        return `<div class="mrow"><div><b>${esc(c.name)}</b> <span class="dim">${esc(c.role)} · ${esc(c.skill)}</span>${c.hired ? ' <span class="tag done">on payroll</span>' : ''}</div><div>${btn}</div></div>`;
      }).join('');
      return `<h2>${esc(d.name)}</h2>
        <p><b>${esc(d.rank)}</b> · ${esc(d.focus)}</p>
        <p class="dim">${esc(d.focusBlurb)}</p>
        <p>Reputation ${d.reputation} · ${d.contracts} contract(s) · ${d.memberCount} member(s) · sample contract ${d.samplePayout} cr</p>
        <h3>Roster</h3>${roster || '<p class="dim">No contacts yet.</p>'}`;
    },
    contacts(d) {
      const list = (d.contacts || []).map((c) => {
        const sel = c.id === d.active ? ' active' : '';
        const un = c.unread ? ` <span class="tag">${c.unread} new</span>` : '';
        const team = c.onTeam ? ' <span class="tag done">team</span>' : '';
        return `<button class="opt${sel}" data-act="contact_open" data-id="${esc(c.id)}">${esc(c.name)} — ${esc(c.role)}${un}${team}</button>`;
      }).join('');
      const active = (d.contacts || []).find((c) => c.id === d.active);
      const thread = (d.thread || []).map((m) => `<div class="msg ${m.theirs ? 'theirs' : 'mine'}"><span>${esc(m.text)}</span><span class="dim">${esc((m.at || '').slice(11, 16))}</span></div>`).join('');
      let head = '', actions = '';
      if (active) {
        head = `<h3>${esc(active.name)} <span class="dim">${esc(active.role)} · ${esc(active.city)}</span></h3><p class="dim">${esc(active.blurb)}</p>`;
        actions = `<div class="msginput"><input id="msg-text" maxlength="280" placeholder="Message ${esc(active.name)}…" /><button class="primary" data-act="message_send" data-id="${esc(active.id)}">Send</button></div>
          <div class="rowbtns">${active.onTeam
            ? `<button data-act="team_remove" data-id="${esc(active.id)}">Leave team</button>`
            : `<button data-act="team_add" data-id="${esc(active.id)}">Add to team</button>`}</div>`;
      }
      const teamSize = (d.contacts || []).filter((c) => c.onTeam).length;
      return `<h2>Contacts</h2>
        <p class="dim">${d.totalUnread} unread · team ${teamSize}/${d.teamMax}. These are the town's written NPCs. Real accounts and live chat live in <b>Town &amp; chat</b> (T).</p>
        <div class="social"><div class="clist">${list}</div><div class="cthread">${head}${thread || '<p class="dim">No messages yet.</p>'}${actions}</div></div>`;
    },
    home(d) {
      const slots = (d.slots || []).map((s) => {
        if (s.kind === 'trophy') return '';
        const opts = (s.options || []).map((o) => `<option value="${esc(o.id)}">${esc(o.name)}</option>`).join('');
        const body = s.empty ? '<span class="dim">empty</span>' : `<b>${esc(s.name)}</b>`;
        const clear = s.empty ? '' : `<button data-act="home_clear" data-slot="${esc(s.id)}">Clear</button>`;
        const pick = opts ? `<select data-slot-pick="${esc(s.id)}">${opts}</select><button data-act="home_place" data-slot="${esc(s.id)}">Place</button>` : '';
        return `<div class="mrow"><div><span class="tag">${esc(s.kind)}</span> ${body}</div><div>${pick} ${clear}</div></div>`;
      }).join('');
      const trophies = (d.trophies || []).map((t) => `<button class="opt" data-act="trophy" data-id="${esc(t.id)}">${t.shown ? '✓ ' : ''}${esc(t.id)}</button>`).join('');
      const b = d.benefits || {};
      return `<h2>${esc(d.mapName || 'Your Home')}</h2>
        <p class="dim">${d.here ? 'You are home.' : 'Viewing your apartment from away — walk home to enjoy it.'}</p>
        <p>Home lab tier ${b.labTier || 0} · +${b.buildSpeed || 0}% build speed · ${b.comfort || 0} comfort · ${b.trophies || 0} troph${(b.trophies || 0) === 1 ? 'y' : 'ies'} on display</p>
        <h3>Slots</h3>${slots || '<p class="dim">No slots.</p>'}
        <h3>Display achievements</h3><p>${trophies || '<span class="dim">Earn some first.</span>'}</p>`;
    },
    settings(d) {
      const a = (d && d.a11y) || {};
      const cb = (id, label, on, hint) =>
        `<label class="toggle"><input type="checkbox" id="${id}" ${on ? 'checked' : ''}/> ${label}${hint ? ` <span class="dim">${hint}</span>` : ''}</label>`;
      return `<h2>Settings</h2>
        <h3>Accessibility</h3>
        <label>UI scale <input type="range" id="uiscale" min="80" max="130" value="${a.uiScale || 100}" /> <span id="uiscale-v">${a.uiScale || 100}%</span></label>
        <label>Text size <input type="range" id="textscale" min="80" max="150" value="${a.textScale || 100}" /> <span id="textscale-v">${a.textScale || 100}%</span></label>
        ${cb('a11y-reduced', 'Reduced motion', a.reducedMotion, 'stops bobbing and flashing')}
        ${cb('a11y-colorblind', 'Color-blind safe palette', a.colorblindPalette, 'never colour-only')}
        ${cb('a11y-contrast', 'High contrast UI', a.highContrast)}
        ${cb('a11y-shake', 'Screen shake', a.screenShake, 'off is easier on the eyes')}
        <h3>Controls</h3><p class="dim">Click a binding, then press a key.</p>
        <div id="binds"></div>
        <h3>Interaction</h3><p class="dim">NPCs talk, objects glow teal when in range. Nothing is signalled by colour alone.</p>
        <h3>Safety</h3><p class="dim">Town chat is rate-limited and only shows real signed-in accounts. Block / report are not implemented yet.</p>`;
    },
    town(d) {
      const me = d.me || {};
      const online = (d.online || []).length;
      return `<h2>Starter Town <span class="dim">${online} online</span></h2>
        <p class="dim">One shared world: everyone here is a real account. Your home is unit
          <b>#${esc(String(me.homeUnit || '—'))}</b> in Home Row.</p>
        ${d.signedIn ? '' : '<p class="cloud-err">Sign in to the platform server to meet other players and chat.</p>'}
        <div class="social town-grid">
          <div class="clist"><h3>In town</h3><div id="town-online">${townOnlineHtml(d)}</div></div>
          <div class="cthread-wrap"><h3>Town chat</h3>
            <div class="cthread" id="town-chat">${townChatHtml(d)}</div>
            <div class="msginput">
              <input id="town-text" maxlength="280" placeholder="Say something to the town…" ${d.signedIn ? '' : 'disabled'} />
              <button class="primary" data-act="town_send" ${d.signedIn ? '' : 'disabled'}>Send</button>
            </div>
          </div>
        </div>
        <div id="town-friends">${townFriendsHtml(d)}</div>
        <div class="rowbtns">
          <button data-act="town_appearance">Edit appearance</button>
          <button data-act="town_refresh">Refresh</button>
        </div>`;
    },
    appearance(d) {
      const a = d.appearance || {};
      return `<h2>Your Character</h2>
        <p class="dim">This is how other real players see you in Starter Town. It is saved to your account, so it follows you to any device.</p>
        ${d.signedIn ? '' : '<p class="cloud-err">Sign in to the platform server to save your look.</p>'}
        <div class="appearance-grid">
          ${colorField('skin', 'Skin', a.skin)}
          ${colorField('hair', 'Hair', a.hair)}
          ${colorField('shirt', 'Shirt', a.shirt)}
          ${colorField('pants', 'Trousers', a.pants)}
        </div>
        <div class="rowbtns">
          <button class="primary" data-act="appearance_save" ${d.signedIn ? '' : 'disabled'}>Save look</button>
          <button data-act="appearance_town">Back to town</button>
        </div>
        <p class="dim">Character colours are cosmetic only. They never change a test verdict or progression.</p>`;
    },
    computer(d) {
      const p = d.profile || {};
      const c = d.cloud || {};
      const me = c.me || {};
      const app = d.app || 'desktop';
      const online = (c.online || []).length;
      const completed = (me.completedMissions || []).length;
      const icon = (act, glyph, label, extra) =>
        `<button class="crt-icon" data-act="${act}"${extra || ''}><i>${glyph}</i><span>${label}</span></button>`;
      let main = '';
      if (app === 'terminal') {
        const lines = d.lines && d.lines.length ? d.lines : [
          'DEVWORLD OS 0.9 — NOVA-80 shell',
          'Type "help" for commands.'
        ];
        main = `<div class="crt-window crt-term-window">
          <div class="crt-title">terminal — ~/${esc(me.handle || 'dev')} <span>— □ ×</span></div>
          <div class="crt-body crt-term" id="crt-term">${lines.map((l) => `<p>${esc(l)}</p>`).join('')}</div>
          <div class="crt-prompt"><span>&gt;</span><input id="crt-cmd" autocomplete="off" spellcheck="false" placeholder="help" /></div>
        </div>`;
      } else if (app === 'notes') {
        const text = d.notes || '';
        main = `<div class="crt-window crt-notes-window">
          <div class="crt-title">notes.txt <span>saved on this machine</span></div>
          <textarea id="crt-notes" spellcheck="true" placeholder="Scratch space for the next build…">${esc(text)}</textarea>
          <div class="crt-note-foot">${text.length} characters · local to this browser</div>
        </div>`;
      } else {
        const last = (c.chat || []).slice(-1)[0];
        main = `<div class="crt-window">
          <div class="crt-title">welcome.txt <span>— □ ×</span></div>
          <div class="crt-body">
            <p><b>DEVWORLD OS 0.9</b> · one town, one machine, one operator.</p>
            <p class="dim">&gt; operator: ${esc(p.displayName || p.name || me.displayName || 'Dev')}</p>
            <p class="dim">&gt; level ${p.level || 1} · ${p.xp || 0}/${p.need || 100} XP · ${completed} missions shipped</p>
            <p class="dim">&gt; credits ${Number(c.balance || 0).toLocaleString()} · home unit #${esc(String(me.homeUnit || '—'))}</p>
            <p class="dim">&gt; town link: ${c.connected ? 'ONLINE' : 'OFFLINE'} · ${online} other account${online === 1 ? '' : 's'} in world</p>
            ${last ? `<p class="crt-newmsg">&gt; ${esc((last.from || {}).displayName || 'Town')}: ${esc(last.text || '')}</p>` : ''}
            <p class="crt-blink">&gt; choose a program _</p>
          </div>
        </div>
        <div class="crt-window small">
          <div class="crt-title">sys.log</div>
          <div class="crt-body">
            <p class="dim">[ok] renderer online</p>
            <p class="dim">[ok] ledger synced · ${Number(c.balance || 0).toLocaleString()} cr</p>
            <p class="dim">[ok] town socket ${c.connected ? 'connected' : 'idle'}</p>
            <p class="dim">[ok] friends ${(c.friends || []).length} · missions ${completed}</p>
            <p class="dim">[--] ${online ? 'people nearby' : 'quiet plaza'}</p>
          </div>
        </div>`;
      }
      return `<div class="crt">
        <div class="crt-top">
          <span class="crt-brand">NOVA-80 // HOME TERMINAL</span>
          <span class="crt-led"></span>
          <span class="crt-clock">${clockNow()}</span>
        </div>
        <div class="crt-screen">
          <div class="crt-icons">
            ${icon('computer_ide', '&gt;_', 'Code')}
            ${icon('computer_board', '▤', 'Board')}
            ${icon('computer_town', '◉', 'Town')}
            ${icon('computer_mail', '✉', 'Mail')}
            ${icon('computer_portfolio', '◆', 'Works')}
            ${icon('computer_app', '&gt;_', 'Terminal', ' data-app="terminal"')}
            ${icon('computer_app', '✎', 'Notes', ' data-app="notes"')}
            ${icon('computer_panel', '▦', 'Shop', ' data-panel="shop"')}
            ${icon('computer_panel', '¤', 'Bank', ' data-panel="bank"')}
            ${icon('computer_panel', '✦', 'Skills', ' data-panel="skilltree"')}
            ${icon('computer_panel', '⌘', 'Map', ' data-panel="worldmap"')}
            ${icon('computer_panel', '⚙', 'Settings', ' data-panel="settings"')}
          </div>
          <div class="crt-main">${main}</div>
        </div>
        <div class="crt-task">
          <div>
            <button data-act="computer_close">Shut down</button>
            ${app === 'desktop' ? '' : '<button data-act="computer_desktop">Desktop</button>'}
          </div>
          <span>${app === 'desktop' ? 'Esc closes · Code opens the IDE · Terminal is real' : 'Esc closes · Desktop returns home'}</span>
        </div>
      </div>`;
    },
    phone(d) {
      const c = d.cloud || d;
      const app = d.app || 'home';
      const me = c.me || {};
      const pr = c.progression || {};
      const online = (c.online || []).length;
      const completed = me.completedMissions || [];
      const head = (title, meta) => `<div class="phone-head"><button data-act="phone_app" data-app="home">‹</button><b>${esc(title)}</b><span class="dim">${meta || ''}</span></div>`;
      const dock = [['home', 'Home'], ['messages', 'Chat'], ['people', 'People'], ['me', 'Me']]
        .map(([id, label]) => `<button class="phone-dock-btn${app === id ? ' active' : ''}" data-act="phone_app" data-app="${id}">${esc(label)}</button>`).join('');
      let screen = '';
      if (app === 'messages') {
        screen = `${head('Town chat', online + ' online')}
          <div class="phone-chat" id="phone-chat">${townChatHtml(c)}</div>
          <div class="phone-input"><input id="phone-text" maxlength="280" placeholder="Message the town…" ${c.signedIn ? '' : 'disabled'} /><button class="primary" data-act="phone_send" ${c.signedIn ? '' : 'disabled'}>Send</button></div>`;
      } else if (app === 'people') {
        screen = `${head('People', online + ' online')}
          <div class="phone-scroll">
            <h3>In town</h3><div id="phone-online">${townOnlineHtml(c)}</div>
            <div id="phone-friends">${townFriendsHtml(c)}</div>
          </div>`;
      } else if (app === 'me') {
        screen = `${head('Me', c.connected ? 'live' : 'offline')}
          <div class="phone-scroll">
            <div class="phone-me">
              <div class="phone-avatar" style="background:${rgbHex((me.appearance || {}).shirt || [45, 212, 191])}"></div>
              <b>${esc(me.displayName || me.handle || 'Not signed in')}</b>
              <span class="dim">@${esc(me.handle || '—')} · Home Row #${esc(String(me.homeUnit || '—'))}</span>
              <span class="dim">Level ${pr.level || 1} · ${pr.totalXp || 0} XP · ${Number(c.balance || 0).toLocaleString()} cr</span>
            </div>
            <div class="phone-list">
              <button data-act="phone_look">Edit appearance</button>
              <button data-act="phone_app" data-app="missions">Missions &amp; progress</button>
              <button data-act="phone_app" data-app="wallet">Wallet &amp; ledger</button>
              <button data-act="phone_panel" data-panel="settings">Device settings</button>
            </div>
          </div>`;
      } else if (app === 'missions') {
        const rows = (c.missions || []).map((m) => {
          const done = completed.includes(m.id);
          return `<div class="phone-row">
            <div><b>${esc(m.title)}</b><br><span class="dim">L${m.level || 1} · ${m.xpReward || 0} XP · ${m.creditReward || 0} cr</span></div>
            <span class="tag ${done ? 'done' : ''}">${done ? 'done' : 'open'}</span>
          </div>`;
        }).join('');
        screen = `${head('Missions', completed.length + '/' + (c.missions || []).length)}
          <div class="phone-scroll">
            <div class="phone-list"><button class="primary" data-act="phone_panel" data-panel="missionboard">Open the mission board</button></div>
            <h3>Contract list</h3>
            <div class="phone-rows">${rows || '<p class="dim">No missions loaded.</p>'}</div>
          </div>`;
      } else if (app === 'wallet') {
        const events = (c.events || []).map((e) => `<div class="phone-feed-row"><span>${esc(e.text || '')}</span><span class="dim">${new Date(e.at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}</span></div>`).join('');
        screen = `${head('Wallet', pr.level ? 'level ' + pr.level : '')}
          <div class="phone-scroll">
            <div class="phone-wallet"><span class="dim">AVAILABLE CREDITS</span><b>${Number(c.balance || 0).toLocaleString()} cr</b><span class="dim">Home Row #${esc(String(me.homeUnit || '—'))} · ${completed.length} missions shipped</span></div>
            <div class="phone-list">
              <button data-act="phone_panel" data-panel="bank">Open bank</button>
              <button data-act="phone_panel" data-panel="shop">Open shop</button>
              <button data-act="phone_panel" data-panel="portfolio">Project portfolio</button>
              <button data-act="phone_panel" data-panel="inventory">Inventory</button>
            </div>
            <h3>Recent server events</h3>
            <div class="phone-feed">${events || '<p class="dim">No server events yet.</p>'}</div>
          </div>`;
      } else if (app === 'pulse') {
        const pulseChat = Object.assign({}, c, { chat: (c.chat || []).slice(-6) });
        screen = `${head('Pulse', c.connected ? 'live' : 'idle')}
          <div class="phone-scroll">
            <div class="phone-wallet small"><b>${online ? online + ' other account' + (online === 1 ? '' : 's') + ' in town' : 'The plaza is quiet'}</b><span class="dim">${c.signedIn ? 'Signed in as @' + esc(me.handle || '—') : 'Not signed in'}</span></div>
            <h3>In town</h3><div id="phone-pulse-online">${townOnlineHtml(c)}</div>
            <h3>Recent chatter</h3><div id="phone-pulse-chat" class="phone-pulse-chat">${townChatHtml(pulseChat)}</div>
            <div class="phone-list"><button data-act="phone_town">Open town panel</button></div>
          </div>`;
      } else if (app === 'settings') {
        screen = `${head('Settings', 'device')}
          <div class="phone-scroll">
            <div class="phone-list">
              <button data-act="phone_panel" data-panel="settings">Display, audio &amp; keybinds</button>
              <button data-act="phone_look">Character appearance</button>
              <button data-act="phone_cloud">${c.signedIn ? 'Cloud account' : 'Sign in to DEVWORLD'}</button>
              <button data-act="phone_town">Town &amp; chat panel</button>
              <button data-act="phone_alerts">Notifications</button>
            </div>
            <div class="phone-feed"><div class="phone-feed-row"><span>Server</span><span class="dim">${c.connected ? 'connected' : 'offline'}</span></div>
              <div class="phone-feed-row"><span>Home Row</span><span class="dim">Unit #${esc(String(me.homeUnit || '—'))}</span></div>
              <div class="phone-feed-row"><span>Account</span><span class="dim">@${esc(me.handle || '—')}</span></div></div>
          </div>`;
      } else {
        screen = `<div class="phone-home">
          <div class="phone-home-top"><span class="dim">Starter Town</span><b>${esc(me.displayName || 'Dev')}</b></div>
          <div class="phone-time">${clockNow()}</div>
          <div class="phone-sub">${online ? online + ' other account' + (online === 1 ? '' : 's') + ' in town' : 'The plaza is quiet'}</div>
          <div class="phone-card">
            <span class="dim">HOME ROW</span>
            <b>Unit #${esc(String(me.homeUnit || '—'))} · Starter Town</b>
            <span class="dim">Level ${pr.level || 1} · ${Number(c.balance || 0).toLocaleString()} cr · ${completed.length} missions</span>
          </div>
          ${(() => { const last = (c.chat || []).slice(-1)[0]; return last ? `<div class="phone-latest"><span class="dim">${esc((last.from || {}).displayName || 'Town')}</span> ${esc(last.text || '')}</div>` : ''; })()}
          <div class="phone-apps">
            <button data-act="phone_app" data-app="messages"><i>✉</i><span>Chat</span><em>${(c.chat || []).length}</em></button>
            <button data-act="phone_app" data-app="people"><i>◉</i><span>People</span><em>${online}</em></button>
            <button data-act="phone_app" data-app="me"><i>☻</i><span>Me</span></button>
            <button data-act="phone_look"><i>◈</i><span>Look</span></button>
            <button data-act="phone_map"><i>▦</i><span>Map</span></button>
            <button data-act="phone_alerts"><i>!</i><span>Alerts</span></button>
            <button data-act="phone_app" data-app="missions"><i>▤</i><span>Missions</span><em>${completed.length}</em></button>
            <button data-act="phone_app" data-app="wallet"><i>◫</i><span>Wallet</span></button>
            <button data-act="phone_app" data-app="pulse"><i>⌁</i><span>Pulse</span></button>
            <button data-act="phone_panel" data-panel="shop"><i>◧</i><span>Shop</span></button>
            <button data-act="phone_panel" data-panel="bank"><i>¤</i><span>Bank</span></button>
            <button data-act="phone_app" data-app="settings"><i>⚙</i><span>Settings</span></button>
          </div>
        </div>`;
      }
      return `<div class="phone">
        <div class="phone-screen">
          <div class="phone-status"><span>${clockNow()}</span><span class="phone-island"></span><span>${c.connected ? '◉' : '○'} ▂▄▆ 100%</span></div>
          ${screen}
          <div class="phone-dock">${dock}</div>
        </div>
      </div>`;
    },
    pausemenu() {
      return `<h2>Paused</h2>
        <button class="opt" data-act="pclose">Resume</button>
        <button class="opt" data-act="pprofile">Profile</button>
        <button class="opt" data-act="pinventory">Inventory &amp; equipment</button>
        <button class="opt" data-act="pquests">Quest log</button>
        <button class="opt" data-act="pmap">World map &amp; city directory</button>
        <button class="opt" data-act="pnotices">Notifications</button>
        <button class="opt" data-act="pskill">Level &amp; skills</button>
        <button class="opt" data-act="pprogression">Progression &amp; unlocks</button>
        <button class="opt" data-act="pportfolio">Project portfolio</button>
        <button class="opt" data-act="ptown">Town &amp; chat</button>
        <button class="opt" data-act="pappearance">Appearance</button>
        <button class="opt" data-act="pcontacts">NPC contacts &amp; team</button>
        <button class="opt" data-act="pcompany">Company</button>
        <button class="opt" data-act="phome">Home &amp; decoration</button>
        <button class="opt" data-act="pcloud">Cloud account</button>
        <button class="opt" data-act="psettings">Settings</button>
        <button class="opt" data-act="psave">Save now</button>`;
    },
    cloud(d) {
      if (!d.signedIn) {
        return `<h2>Cloud Account</h2>
          <p class="dim">Sign in to the DEVWORLD platform server. Your level, XP, credits and completed missions live on the server, not in this browser.</p>
          <label>Server <input id="cloud-base" value="${esc(d.base)}" spellcheck="false" /></label>
          <label>Username <input id="cloud-user" autocomplete="username" maxlength="32" spellcheck="false" /></label>
          <label>Password <input id="cloud-pass" type="password" autocomplete="current-password" /></label>
          <label>Display name <input id="cloud-display" maxlength="32" placeholder="optional" /></label>
          ${d.error ? `<p class="cloud-err">${esc(d.error)}</p>` : ''}
          <div class="cloud-actions">
            <button class="primary" id="cloud-login">Sign in</button>
            <button id="cloud-register">Create account</button>
            <button id="cloud-save">Save server</button>
          </div>
          <p class="dim">${d.connected ? 'Live socket connected.' : 'Not connected to the platform server.'}</p>`;
      }
      const p = d.player || {};
      const pr = d.progression || {};
      const need = pr.xpForNextLevel || 0;
      const pct = need > 0 ? Math.min(100, Math.round((pr.xpIntoLevel || 0) / need * 100)) : 100;
      const done = (p.completedMissions || []).length;
      const events = (d.events || []).map((e) => `<div class="cloud-event">${esc(e.text)}</div>`).join('');
      return `<h2>Cloud Account</h2>
        <div class="cloud-head">
          <div><b>${esc(p.displayName || p.handle || 'Dev')}</b> <span class="dim">@${esc(p.handle || '')}</span></div>
          <span class="tag ${d.connected ? 'done' : ''}">${d.connected ? 'live' : 'offline'}</span>
        </div>
        <div class="cloud-stats">
          <div><span class="dim">Level</span><b>${pr.level || 1}</b></div>
          <div><span class="dim">Title</span><b>${esc(pr.title || '')}</b></div>
          <div><span class="dim">Credits</span><b>${Number(d.balance || 0).toLocaleString()}</b></div>
          <div><span class="dim">Missions</span><b>${done}</b></div>
        </div>
        <div class="cloud-bar"><span style="width:${pct}%"></span></div>
        <p class="dim">${pr.xpIntoLevel || 0} / ${need} XP into level ${pr.level || 1} · ${pr.totalXp || 0} XP total</p>
        <h3>Recent server events</h3>
        <div class="cloud-events">${events || '<p class="dim">No live events yet. Complete a mission and it will appear here.</p>'}</div>
        ${d.error ? `<p class="cloud-err">${esc(d.error)}</p>` : ''}
        <div class="cloud-actions">
          <button class="primary" id="cloud-refresh">Refresh</button>
          <button id="cloud-logout">Sign out</button>
        </div>
        <p class="dim">Server: ${esc(d.base)}</p>`;
    },
    idebar(d) {
      return `<div class="idebar-inner">
        <span class="ide-title">${d.title ? (d.practice ? 'Practice: ' : 'Mission: ') + esc(d.title) : 'Practice machine'}</span>
        ${d.mission ? `<button class="primary" id="ide-submit" data-mid="${esc(d.mission)}">Submit Solution</button>
        <button id="ide-verify" data-mid="${esc(d.mission)}">Verify on Server</button>` : ''}
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
      (['charcreate', 'phone', 'computer'].includes(id) ? '' : '<button class="closebtn" data-act="close">Close (Esc)</button>');
    if (id === 'charcreate') wireCharCreate(data);
    if (id === 'settings') wireSettings(data);
    if (id === 'cloud') wireCloud(p);
    if (id === 'town') wireTown(p);
    if (id === 'phone') wirePhone(p);
    if (id === 'computer') wireComputer(p);
    wirePanel(p);
    open(id);
  }
  let currentNpc = '';
  function setNpc(id) { currentNpc = id; }

  function wireTown(root) {
    const input = root.querySelector('#town-text');
    if (input) {
      input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
          e.preventDefault();
          const b = root.querySelector('[data-act="town_send"]');
          if (b) b.click();
        }
      });
    }
    const c = root.querySelector('#town-chat');
    if (c) c.scrollTop = c.scrollHeight;
  }

  // Paint only the live parts of the town panel so typing is never interrupted
  // by incoming movement or chat.
  function paintTown(d) {
    const online = document.getElementById('town-online');
    if (online) online.innerHTML = townOnlineHtml(d);
    const chat = document.getElementById('town-chat');
    if (chat) {
      const atBottom = chat.scrollHeight - chat.scrollTop - chat.clientHeight < 48;
      chat.innerHTML = townChatHtml(d);
      if (atBottom) chat.scrollTop = chat.scrollHeight;
    }
    const friends = document.getElementById('town-friends');
    if (friends) friends.innerHTML = townFriendsHtml(d);
  }

  function wirePhone(root) {
    const input = root.querySelector('#phone-text');
    if (input) {
      input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
          e.preventDefault();
          const b = root.querySelector('[data-act="phone_send"]');
          if (b) b.click();
        }
      });
    }
    const chat = root.querySelector('#phone-chat');
    if (chat) chat.scrollTop = chat.scrollHeight;
  }

  function paintPhone(d) {
    const chat = document.getElementById('phone-chat');
    if (chat) {
      const atBottom = chat.scrollHeight - chat.scrollTop - chat.clientHeight < 48;
      chat.innerHTML = townChatHtml(d);
      if (atBottom) chat.scrollTop = chat.scrollHeight;
    }
    const online = document.getElementById('phone-online');
    if (online) online.innerHTML = townOnlineHtml(d);
    const friends = document.getElementById('phone-friends');
    if (friends) friends.innerHTML = townFriendsHtml(d);
    const pulse = document.getElementById('phone-pulse-online');
    if (pulse) pulse.innerHTML = townOnlineHtml(d);
    const pulseChat = document.getElementById('phone-pulse-chat');
    if (pulseChat) {
      const recent = Object.assign({}, d, { chat: (d.chat || []).slice(-6) });
      pulseChat.innerHTML = townChatHtml(recent);
    }
  }

  function wireComputer(root) {
    const cmd = root.querySelector('#crt-cmd');
    if (cmd) {
      setTimeout(() => cmd.focus(), 40);
      cmd.addEventListener('keydown', (e) => {
        if (e.key !== 'Enter') return;
        e.preventDefault();
        const text = cmd.value;
        cmd.value = '';
        DW.computer.command(text);
      });
    }
    const notes = root.querySelector('#crt-notes');
    if (notes) notes.addEventListener('input', () => DW.computer.saveNotes(notes.value));
  }

  function wirePanel(root) {
    // Delegated: panels repaint their live sections (town list/chat), so
    // binding each button at render time would miss every new element.
    if (!root.__dwDelegated) {
      root.__dwDelegated = true;
      root.addEventListener('click', (e) => {
        const b = e.target.closest('[data-act]');
        if (!b || !root.contains(b)) return;
        const a = b.getAttribute('data-act');
        if (a === 'close') close('all');
        else if (a === 'accept') send('mission_accept', { id: b.getAttribute('data-id') });
        else if (a === 'practice') send('practice', { id: b.getAttribute('data-id') });
        else if (a === 'buy') send('shop_buy', { id: b.getAttribute('data-id') });
        else if (a === 'equip') send('equip', { id: b.getAttribute('data-id') });
        else if (a === 'unequip') send('unequip', { slot: b.getAttribute('data-slot') });
        else if (a === 'track') send('track', { id: b.getAttribute('data-id') });
        else if (a === 'notifications_clear') send('notifications_clear');
        else if (a === 'travel') { send('travel', { id: b.getAttribute('data-id') }); notify('Riding the bus…', 'info'); }
        else if (a === 'dopt') send('dialogue_choice', { npc: currentNpc, next: b.getAttribute('data-next'), sets: b.getAttribute('data-sets') || '' });
        else if (a === 'home_place') {
          const sel = b.parentElement.querySelector('select');
          if (sel && sel.value) send('home_place', { slot: b.getAttribute('data-slot'), item: sel.value });
        }
        else if (a === 'home_clear') send('home_clear', { slot: b.getAttribute('data-slot') });
        else if (a === 'trophy') send('trophy', { id: b.getAttribute('data-id') });
        else if (a === 'contact_open') send('contact_open', { id: b.getAttribute('data-id') });
        else if (a === 'message_send') {
          const inp = document.getElementById('msg-text');
          if (inp && inp.value.trim()) send('message', { contact: b.getAttribute('data-id'), text: inp.value });
        }
        else if (a === 'team_add' || a === 'team_remove') {
          send(a, { id: b.getAttribute('data-id'), contact: b.getAttribute('data-id') });
        }
        else if (a === 'hire') send('hire', { id: b.getAttribute('data-id'), add: true });
        else if (a === 'unhire') send('hire', { id: b.getAttribute('data-id'), add: false });
        else if (a === 'company_found') {
          const nm = document.getElementById('co-name');
          const fc = document.getElementById('co-focus');
          send('company_found', { name: nm ? nm.value : '', focus: fc ? fc.value : '' });
        }
        else if (a === 'pclose') close('all');
        else if (a === 'pprofile') { close('all'); requestPanel('profile'); }
        else if (a === 'pinventory') { close('all'); requestPanel('inventory'); }
        else if (a === 'pquests') { close('all'); requestPanel('quests'); }
        else if (a === 'pmap') { close('all'); requestPanel('worldmap'); }
        else if (a === 'pnotices') { close('all'); requestPanel('notifications'); }
        else if (a === 'pskill') { close('all'); requestPanel('skilltree'); }
        else if (a === 'pprogression') { close('all'); requestPanel('progression'); }
        else if (a === 'choose_career') send('choose_career', { id: b.getAttribute('data-id') });
        else if (a === 'pportfolio') { close('all'); requestPanel('portfolio'); }
        else if (a === 'ptown') { close('all'); DW.cloud.openTown(); }
        else if (a === 'pappearance') { close('all'); DW.cloud.openAppearance(); }
        else if (a === 'pcontacts') { close('all'); requestPanel('contacts'); }
        else if (a === 'pcompany') { close('all'); requestPanel('company'); }
        else if (a === 'phome') { close('all'); requestPanel('home'); }
        else if (a === 'psettings') { close('all'); requestPanel('settings'); }
        else if (a === 'pcloud') { close('all'); DW.cloud.openPanel(); }
        else if (a === 'psave') { send('save'); notify('Progress saved.', 'success'); close('all'); }
        else if (a === 'town_send') {
          const inp = document.getElementById('town-text');
          if (inp && inp.value.trim() && DW.cloud.sendChat(inp.value.trim())) inp.value = '';
        }
        else if (a === 'town_add') {
          DW.cloud.requestFriend(b.getAttribute('data-id'))
            .then(() => notify('Friend request sent.', 'success'))
            .catch((e) => notify(e.message || 'Could not send request.', 'error'));
        }
        else if (a === 'town_accept') {
          DW.cloud.acceptFriend(b.getAttribute('data-id'))
            .then(() => notify('You are now friends.', 'success'))
            .catch((e) => notify(e.message || 'Could not accept.', 'error'));
        }
        else if (a === 'town_decline') {
          DW.cloud.declineFriend(b.getAttribute('data-id'))
            .then(() => notify('Request declined.', 'info'))
            .catch((e) => notify(e.message || 'Could not decline.', 'error'));
        }
        else if (a === 'town_remove') {
          DW.cloud.removeFriend(b.getAttribute('data-id'))
            .then(() => notify('Friend removed.', 'info'))
            .catch((e) => notify(e.message || 'Could not remove.', 'error'));
        }
        else if (a === 'town_refresh') { close('all'); DW.cloud.openTown(); }
        else if (a === 'town_appearance') { close('all'); DW.cloud.openAppearance(); }
        else if (a === 'appearance_town') { close('all'); DW.cloud.openTown(); }
        else if (a === 'phone_send') {
          const inp = document.getElementById('phone-text');
          if (inp && inp.value.trim() && DW.cloud.sendChat(inp.value.trim())) inp.value = '';
        }
        else if (a === 'phone_app') {
          DW.phone.go(b.getAttribute('data-app') || 'home');
        }
        else if (a === 'phone_look') { DW.phone.close(); DW.cloud.openAppearance(); }
        else if (a === 'phone_map') { DW.phone.close(); requestPanel('worldmap'); cppEvent('panel_opened'); }
        else if (a === 'phone_alerts') { DW.phone.close(); requestPanel('notifications'); cppEvent('panel_opened'); }
        else if (a === 'phone_cloud') { DW.phone.close(); DW.cloud.openPanel(); }
        else if (a === 'phone_panel') {
          DW.phone.close();
          requestPanel(b.getAttribute('data-panel') || 'settings');
          cppEvent('panel_opened');
        }
        else if (a === 'phone_town') { DW.phone.close(); DW.cloud.openTown(); }
        else if (a === 'computer_ide') { Module.ccall('dw_computer_action', null, ['string'], ['ide']); }
        else if (a === 'computer_board') { Module.ccall('dw_computer_action', null, ['string'], ['board']); }
        else if (a === 'computer_portfolio') { Module.ccall('dw_computer_action', null, ['string'], ['portfolio']); }
        else if (a === 'computer_mail') { Module.ccall('dw_computer_action', null, ['string'], ['mail']); }
        else if (a === 'computer_app') { DW.computer.app(b.getAttribute('data-app') || 'desktop'); }
        else if (a === 'computer_panel') {
          Module.ccall('dw_computer_action', null, ['string'], ['panel:' + (b.getAttribute('data-panel') || 'settings')]);
        }
        else if (a === 'computer_desktop') { DW.computer.desktop(); }
        else if (a === 'computer_town') {
          Module.ccall('dw_computer_action', null, ['string'], ['town']);
          DW.cloud.openTown();
        }
        else if (a === 'computer_close') { close('all'); }
        else if (a === 'appearance_save') {
          const col = (id) => {
            const el = document.getElementById(id);
            const h = (el && el.value) || '#000000';
            return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
          };
          DW.cloud.saveAppearance({ skin: col('app-skin'), hair: col('app-hair'), shirt: col('app-shirt'), pants: col('app-pants') })
            .then(() => notify('Look saved to your account.', 'success'))
            .catch((e) => notify(e.message || 'Could not save your look.', 'error'));
        }
      });
    }
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
    const v = document.getElementById('ide-verify');
    if (v) v.addEventListener('click', async () => {
      const mid = v.getAttribute('data-mid');
      if (!DW.cloud.state().signedIn) {
        DW.cloud.openPanel();
        notify('Sign in to verify this mission on the platform server.', 'info');
        return;
      }
      const label = v.textContent;
      v.disabled = true;
      v.textContent = 'Verifying…';
      notify('Sending your workspace to the platform server…', 'info');
      try {
        const r = await DW.cloud.submitMission(mid);
        if (r.passed) {
          const pr = r.progression || {};
          notify('Server verified: ' + r.publicPassed + ' public + ' + r.hiddenPassed +
            ' hidden passed · ' + (pr.totalXp || 0) + ' XP · ' + r.balance + ' cr', 'success');
        } else {
          const fail = (r.results || []).find((x) => !x.passed && !x.hidden);
          notify('Server verdict: not passed (' + r.publicPassed + ' public + ' +
            r.hiddenPassed + ' hidden)' + (fail ? ' — ' + fail.name : ''), 'error');
        }
      } catch (e) {
        if (e.code === 'signed-out') {
          DW.cloud.openPanel();
          notify(e.message, 'info');
        } else if (e.status === 409) {
          notify('Already completed on the platform server.', 'info');
        } else if (e.status === 401) {
          notify('Session expired — sign in again.', 'error');
        } else if (e.status === 503) {
          notify('Server grading is offline: ' + e.message, 'error');
        } else {
          notify(e.message || 'Server verification failed.', 'error');
        }
      } finally {
        v.disabled = false;
        v.textContent = label;
      }
    });
    const b = document.getElementById('ide-back');
    if (b) b.addEventListener('click', () => DW.ide.close());
  }

  const CC = { skin: 0, shirt: 0, pants: 0, hair: 0 };
  function wireCharCreate() {
    const skins = [[232,190,150],[210,170,130],[160,110,80],[240,200,160]];
    const shirts = [[45,212,191],[200,80,80],[80,160,200],[200,170,60],[140,120,200]];
    const pants = [[51,65,85],[30,30,40],[110,80,55],[70,90,60]];
    const hairc = [[45,35,28],[90,60,40],[180,140,90],[30,30,34],[150,150,150],[160,60,70]];
    const colors = () => ({
      skin: skins[CC.skin], shirt: shirts[CC.shirt],
      pants: pants[CC.pants], hair: hairc[CC.hair]
    });
    const preview = () => paintAvatar(document.getElementById('cc-avatar'), colors(), { full: true });
    preview();

    document.querySelectorAll('#panel-charcreate .sw').forEach((b) => {
      b.addEventListener('click', () => {
        CC[b.getAttribute('data-k')] = parseInt(b.getAttribute('data-i'));
        b.parentElement.querySelectorAll('.sw').forEach((x) => x.classList.remove('sel'));
        b.classList.add('sel');
        preview();
      });
    });
    const name = document.getElementById('cc-name');
    const display = document.getElementById('cc-display');
    const previewName = document.getElementById('cc-preview-name');
    name.addEventListener('input', () => {
      if (previewName) previewName.textContent = name.value || 'Dev';
    });
    display.addEventListener('input', () => {
      if (previewName) previewName.textContent = display.value || name.value || 'Dev';
    });
    document.getElementById('cc-go').addEventListener('click', () => {
      send('char_done', {
        name: name.value || 'Dev',
        display: display.value || 'Dev',
        skin: skins[CC.skin], shirt: shirts[CC.shirt], pants: pants[CC.pants], hair: hairc[CC.hair]
      });
    });
  }
  function collectA11y() {
    const g = (id) => document.getElementById(id);
    return {
      uiScale: parseInt(g('uiscale').value, 10),
      textScale: parseInt(g('textscale').value, 10),
      reducedMotion: g('a11y-reduced').checked,
      colorblindPalette: g('a11y-colorblind').checked,
      highContrast: g('a11y-contrast').checked,
      screenShake: g('a11y-shake').checked
    };
  }
  function pushA11y() {
    const a = collectA11y();
    applyA11y(a);                    // instant feedback, before the round-trip
    send('a11y', { a11y: a });
  }
  const KEY_NAMES = { 87: 'W', 65: 'A', 83: 'S', 68: 'D', 69: 'E', 77: 'M', 74: 'J',
    73: 'I', 80: 'P', 78: 'N', 27: 'Esc', 37: '←', 38: '↑', 39: '→', 40: '↓', 32: 'Space' };
  function keyName(code) { return KEY_NAMES[code] || ('key ' + code); }

  function wireSettings(d) {
    const binds = (d && d.keybinds) || {};
    const acts = [['up', 'Move up'], ['down', 'Move down'], ['left', 'Move left'],
      ['right', 'Move right'], ['interact', 'Interact'], ['map', 'World map'],
      ['quests', 'Quest log'], ['inventory', 'Inventory'], ['profile', 'Profile'],
      ['notifications', 'Notifications']];
    const box = document.getElementById('binds');
    if (box) {
      box.innerHTML = acts.map((a) => `<button class="opt" data-bind="${a[0]}">${a[1]}: <span>${esc(keyName(binds[a[0]]))}</span></button>`).join('');
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
    }

    const ui = document.getElementById('uiscale');
    const tx = document.getElementById('textscale');
    const live = () => applyA11y(collectA11y());
    ui.addEventListener('input', () => { document.getElementById('uiscale-v').textContent = ui.value + '%'; live(); });
    tx.addEventListener('input', () => { document.getElementById('textscale-v').textContent = tx.value + '%'; live(); });
    ui.addEventListener('change', pushA11y);
    tx.addEventListener('change', pushA11y);
    ['a11y-reduced', 'a11y-colorblind', 'a11y-contrast', 'a11y-shake'].forEach((id) => {
      document.getElementById(id).addEventListener('change', pushA11y);
    });
  }

  function wireCloud(root) {
    const q = (id) => root.querySelector('#' + id);
    const val = (id) => (q(id) && q(id).value ? q(id).value : '');
    const baseInput = q('cloud-base');

    // Enter in any credential field submits the form.
    ['cloud-user', 'cloud-pass', 'cloud-display'].forEach((id) => {
      const input = q(id);
      if (!input) return;
      input.addEventListener('keydown', (e) => {
        if (e.key !== 'Enter') return;
        e.preventDefault();
        const loginBtn = q('cloud-login');
        if (loginBtn) loginBtn.click();
      });
    });

    const save = q('cloud-save');
    if (save) save.addEventListener('click', () => {
      DW.cloud.setBase(baseInput ? baseInput.value : '');
      notify('Server URL saved.', 'success');
    });

    const login = q('cloud-login');
    if (login) login.addEventListener('click', async () => {
      const user = val('cloud-user').trim();
      const pass = val('cloud-pass');
      if (baseInput) DW.cloud.setBase(baseInput.value);
      try {
        await DW.cloud.login(user, pass);
        notify('Signed in to the platform server.', 'success');
      } catch (e) { /* the store re-renders the panel with the error */ }
    });

    const reg = q('cloud-register');
    if (reg) reg.addEventListener('click', async () => {
      const user = val('cloud-user').trim();
      const pass = val('cloud-pass');
      const display = val('cloud-display').trim();
      if (baseInput) DW.cloud.setBase(baseInput.value);
      try {
        await DW.cloud.register(user, pass, display);
        notify('Account created. Welcome to the platform.', 'success');
      } catch (e) { /* the store re-renders the panel with the error */ }
    });

    const refresh = q('cloud-refresh');
    if (refresh) refresh.addEventListener('click', () => {
      DW.cloud.refresh().then(() => notify('Profile refreshed.', 'info')).catch(() => {});
    });

    const logout = q('cloud-logout');
    if (logout) logout.addEventListener('click', () => {
      DW.cloud.logout();
      notify('Signed out.', 'info');
    });
  }

  return { show, hide: close, notify, updateHud, paintTown, paintPhone, setNpc, esc, request: requestPanel };
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

// ---- cloud account: the live C++ platform server -------------------------
// The browser talks to the server directly: fetch for JSON, a WebSocket for
// live progression events. The token is opaque and stored locally; the server
// owns the password hash, XP and ledger.
DW.cloud = (() => {
  const LS_BASE = 'devworld.backend';
  const LS_TOKEN = 'devworld.token';
  const DEFAULT_BASE = location.protocol === 'file:'
    ? 'http://127.0.0.1:8091'
    : (location.hostname
      ? location.protocol + '//' + location.hostname + ':8091'
      : 'http://127.0.0.1:8091');

  let base = localStorage.getItem(LS_BASE) || DEFAULT_BASE;
  let token = localStorage.getItem(LS_TOKEN) || '';
  let socket = null;
  let reconnectTimer = null;
  let manualClose = false;
  let panelOff = null;
  let myId = '';
  const world = new Map();
  const chat = [];
  const friends = { friends: [], incoming: [], outgoing: [] };

  const listeners = new Set();
  const events = [];
  const state = {
    base, token: !!token, signedIn: false, connected: false,
    player: null, progression: null, balance: 0, missions: [],
    online: 0, events, error: ''
  };

  function emit() {
    state.base = base;
    state.token = !!token;
    listeners.forEach((fn) => { try { fn(state); } catch (e) {} });
  }
  function onChange(fn) {
    listeners.add(fn);
    return () => listeners.delete(fn);
  }
  function snapshot() {
    return Object.assign({}, state, { events: events.slice(-8).reverse() });
  }
  function logEvent(text) {
    events.push({ text, at: Date.now() });
    while (events.length > 20) events.shift();
  }

  function meId() { return (state.player && state.player.id) || myId || ''; }

  function applyAppearance(p) {
    if (!p || !p.appearance) return;
    try {
      if (window.Module && Module.ccall) {
        Module.ccall('dw_set_appearance', null, ['string'], [JSON.stringify(p.appearance)]);
      }
    } catch (e) {}
  }

  function localAppearance() {
    try {
      if (!window.Module || !Module.ccall) return null;
      return JSON.parse(Module.UTF8ToString(Module.ccall('dw_get_appearance', 'number', [], [])));
    } catch (e) { return null; }
  }

  function normPlayer(p) {
    if (!p) return p;
    return Object.assign({}, p, { id: p.id || p.playerId || '' });
  }

  function forwardWorld(msg) {
    try {
      if (window.Module && Module.ccall) {
        Module.ccall('dw_world_event', null, ['string'], [JSON.stringify(msg)]);
      }
    } catch (e) {}
  }

  function townData() {
    return {
      signedIn: state.signedIn,
      connected: state.connected,
      me: state.player,
      progression: state.progression,
      balance: state.balance,
      missions: state.missions,
      events: events.slice(-8).reverse(),
      online: Array.from(world.values()),
      chat: chat.slice(-60),
      friends: friends.friends,
      incoming: friends.incoming,
      outgoing: friends.outgoing,
      error: state.error
    };
  }

  function paintTown() {
    if (!DW.ui) return;
    const ov = document.getElementById('ov-town');
    if (ov && ov.classList.contains('open') && DW.ui.paintTown) DW.ui.paintTown(townData());
    if (DW.phone && DW.phone.isOpen()) DW.phone.paint();
  }

  async function refreshFriends() {
    if (!token) return null;
    try {
      const f = await api('/friends');
      friends.friends = f.friends || [];
      friends.incoming = f.incoming || [];
      friends.outgoing = f.outgoing || [];
      paintTown();
      return f;
    } catch (e) {
      if (e.status === 401) clearToken();
      throw e;
    }
  }

  async function api(path, opts) {
    opts = opts || {};
    const headers = Object.assign({}, opts.headers || {});
    if (opts.body != null) headers['Content-Type'] = 'application/json';
    if (token) headers['Authorization'] = 'Bearer ' + token;
    let res;
    try {
      res = await fetch(base + path, Object.assign({}, opts, { headers }));
    } catch (e) {
      throw new Error('Cannot reach ' + base);
    }
    let body = null;
    try { body = await res.json(); } catch (e) {}
    if (!res.ok) {
      const err = new Error((body && body.error) || ('HTTP ' + res.status));
      err.status = res.status;
      err.body = body;
      throw err;
    }
    return body;
  }

  function applyLive(msg) {
    if (msg.player) state.player = msg.player;
    if (msg.progression) state.progression = msg.progression;
    if (typeof msg.balance === 'number') state.balance = msg.balance;
    state.signedIn = true;
  }

  function handleSocket(msg) {
    if (!msg || !msg.type) return;
    if (msg.type === 'hello') {
      applyLive(msg);
      myId = msg.playerId || myId;
      logEvent('Connected to the platform server.');
      emit();
    } else if (msg.type === 'mission.completed') {
      applyLive(msg);
      const total = msg.progression ? msg.progression.totalXp : 0;
      logEvent('Mission ' + msg.missionId + ' completed · ' + total + ' XP total · ' + msg.balance + ' cr');
      emit();
      DW.ui.notify('Mission ' + msg.missionId + ' completed.', 'success');
      DW.audio.beep('coin');
      // Older servers send no player snapshot; refetch so the mission count
      // and any other profile fields stay in sync.
      if (!msg.player) refresh().catch(() => {});
    } else if (msg.type === 'world.snapshot') {
      world.clear();
      const seen = [];
      (msg.players || []).forEach((raw) => {
        const p = normPlayer(raw);
        if (p && p.id && p.id !== meId()) {
          world.set(p.id, p);
          seen.push(p);
        }
      });
      chat.length = 0;
      (msg.chat || []).forEach((m) => chat.push(m));
      while (chat.length > 100) chat.shift();
      const clean = Object.assign({}, msg, { players: seen });
      forwardWorld(clean);
      if (state.online !== world.size) { state.online = world.size; emit(); }
      paintTown();
      refreshFriends().catch(() => {});
    } else if (msg.type === 'world.move') {
      const p = normPlayer(msg.player);
      if (p && p.id && p.id !== meId()) {
        const isNew = !world.has(p.id);
        world.set(p.id, p);
        if (isNew) { state.online = world.size; emit(); }
        forwardWorld(Object.assign({}, msg, { player: p }));
        paintTown();
      }
    } else if (msg.type === 'world.leave') {
      if (msg.playerId) {
        world.delete(msg.playerId);
        state.online = world.size;
        forwardWorld(msg);
        emit();
        paintTown();
      }
    } else if (msg.type === 'player.updated') {
      const p = msg.player;
      if (p && p.id) {
        if (p.id === meId()) {
          state.player = Object.assign({}, state.player, p);
          applyAppearance(p);
          emit();
        } else if (world.has(p.id)) {
          world.set(p.id, Object.assign({}, world.get(p.id), p));
          forwardWorld(msg);
          paintTown();
        }
      }
    } else if (msg.type === 'chat.message') {
      chat.push(msg);
      while (chat.length > 100) chat.shift();
      paintTown();
    } else if (msg.type === 'chat.error') {
      DW.ui.notify(msg.message || 'Message not sent.', 'error');
    } else if (msg.type === 'friend.request') {
      const who = (msg.from && (msg.from.displayName || msg.from.handle)) || 'Someone';
      DW.ui.notify(who + ' wants to be friends.', 'info');
      refreshFriends().catch(() => {});
    } else if (msg.type === 'friend.accepted') {
      const who = (msg.player && (msg.player.displayName || msg.player.handle)) || 'Someone';
      DW.ui.notify('You are now friends with ' + who + '.', 'success');
      refreshFriends().catch(() => {});
    } else if (msg.type === 'friend.declined' || msg.type === 'friend.removed') {
      refreshFriends().catch(() => {});
    }
  }

  function closeWs(manual) {
    manualClose = !!manual;
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (socket) {
      try { socket.onclose = null; socket.close(); } catch (e) {}
      socket = null;
    }
    state.connected = false;
  }

  function connectWs() {
    closeWs(false);
    if (!token) return;
    manualClose = false;
    const url = base.replace(/^http/, 'ws') + '/ws?token=' + encodeURIComponent(token);
    try { socket = new WebSocket(url); } catch (e) { return; }
    socket.onopen = () => { state.connected = true; emit(); };
    socket.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      handleSocket(msg);
    };
    socket.onerror = () => {};
    socket.onclose = () => {
      state.connected = false;
      emit();
      if (!manualClose && token) {
        reconnectTimer = setTimeout(connectWs, 4000);
      }
    };
  }

  async function refresh() {
    if (!token) return null;
    const me = await api('/me');
    state.player = me;
    state.progression = me.progression || null;
    state.balance = me.balance || 0;
    state.signedIn = true;
    state.error = '';
    applyAppearance(me);
    try {
      const list = await api('/missions');
      state.missions = (list && list.missions) || [];
    } catch (e) { /* keep the last catalogue */ }
    emit();
    return me;
  }

  async function login(username, password) {
    try {
      const r = await api('/auth/login', {
        method: 'POST',
        // Server contract (game-server/src/app.ts): login takes playerId, not username.
        body: JSON.stringify({ playerId: username, password })
      });
      token = r.token;
      localStorage.setItem(LS_TOKEN, token);
      state.error = '';
      if (r.player) state.player = r.player;
      try { await refresh(); } catch (e) {}
      connectWs();
      emit();
      return r;
    } catch (e) {
      state.error = e.message;
      emit();
      throw e;
    }
  }

  async function register(username, password, displayName) {
    const local = localAppearance();
    try {
      // Server contract (game-server/src/app.ts + players.ts): register takes
      // id/handle/password, and returns the player record — no token. The
      // handle must be 3-24 chars of a-z, 0-9 or _, while the id allows
      // hyphens too — so derive the handle by stripping anything the
      // handle rule rejects (e.g. player-1 -> player1); the server's
      // 400/409 still surfaces for names that cannot become a handle.
      // Sign in right after to obtain the token.
      const r = await api('/auth/register', {
        method: 'POST',
        body: JSON.stringify({
          id: username,
          handle: username.toLowerCase().replace(/[^a-z0-9_]/g, ''),
          password,
          ...(displayName ? { displayName } : {}),
        })
      });
      state.error = '';
      if (r && r.id) state.player = r;
      const signed = await login(username, password);
      // A new account inherits the character the player just created here.
      if (local) {
        try { await saveAppearance(local, true); } catch (e) {}
      }
      return signed;
    } catch (e) {
      state.error = e.message;
      emit();
      throw e;
    }
  }

  function reset() {
    state.signedIn = false;
    state.player = null;
    state.progression = null;
    state.balance = 0;
    state.missions = [];
    state.online = 0;
    state.error = '';
    events.length = 0;
    world.clear();
    chat.length = 0;
    friends.friends = [];
    friends.incoming = [];
    friends.outgoing = [];
    myId = '';
    emit();
  }

  async function logout() {
    try { await api('/auth/logout', { method: 'POST', body: '{}' }); } catch (e) {}
    token = '';
    localStorage.removeItem(LS_TOKEN);
    closeWs(true);
    reset();
  }

  function clearToken() {
    token = '';
    localStorage.removeItem(LS_TOKEN);
    closeWs(true);
    reset();
  }

  function setBase(url) {
    let v = String(url || '').trim().replace(/\/+$/, '');
    if (!v) return;
    if (!/^https?:\/\//.test(v)) v = 'http://' + v;
    base = v;
    localStorage.setItem(LS_BASE, base);
    if (token) connectWs();
    emit();
  }

  async function submitMission(id) {
    if (!token) {
      const e = new Error('Sign in to verify on the server.');
      e.code = 'signed-out';
      throw e;
    }
    const G = DW.ide.gameIDE();
    if (!G) throw new Error('The IDE is not ready yet.');
    const paths = (G.listFiles && G.listFiles()) || [];
    const files = [];
    for (const p of paths) {
      let content = null;
      try { content = G.readFile(p); } catch (e) { continue; }
      if (typeof content !== 'string') continue;
      files.push({ path: p, content });
    }
    if (!files.length) throw new Error('The workspace has no readable files.');
    const r = await api('/missions/' + encodeURIComponent(id) + '/submit', {
      method: 'POST',
      body: JSON.stringify({ files })
    });
    if (r.progression) state.progression = r.progression;
    if (typeof r.balance === 'number') state.balance = r.balance;
    state.signedIn = true;
    logEvent('Server ' + id + ': ' + (r.publicPassed || 0) + ' public + ' +
      (r.hiddenPassed || 0) + ' hidden · ' + (r.passed ? 'passed' : 'not passed'));
    // The verdict may have completed the mission; refetch the authoritative
    // profile so the panel's mission count and XP agree with the server.
    try { await refresh(); } catch (e) {}
    emit();
    return r;
  }

  function position(x, y, mapId) {
    if (!socket || socket.readyState !== 1) return;
    try {
      socket.send(JSON.stringify({ type: 'world.state', x, y, mapId }));
    } catch (e) {}
  }

  function sendChat(text) {
    if (!socket || socket.readyState !== 1) {
      DW.ui.notify('Not connected to the platform server.', 'error');
      return false;
    }
    try {
      socket.send(JSON.stringify({ type: 'chat.send', text }));
      return true;
    } catch (e) {
      DW.ui.notify('Message not sent.', 'error');
      return false;
    }
  }

  async function saveAppearance(app, silent) {
    if (!token) {
      const e = new Error('Sign in to save your look.');
      e.code = 'signed-out';
      throw e;
    }
    const r = await api('/me/appearance', {
      method: 'PUT',
      body: JSON.stringify(app)
    });
    if (r.player) {
      state.player = r.player;
      applyAppearance(r.player);
    }
    state.error = '';
    if (!silent) {
      logEvent('Character appearance saved.');
      emit();
    }
    return r;
  }

  async function requestFriend(playerId) {
    const r = await api('/friends/request', {
      method: 'POST',
      body: JSON.stringify({ playerId })
    });
    await refreshFriends();
    return r;
  }

  async function acceptFriend(playerId) {
    const r = await api('/friends/accept', {
      method: 'POST',
      body: JSON.stringify({ playerId })
    });
    await refreshFriends();
    return r;
  }

  async function declineFriend(playerId) {
    const r = await api('/friends/decline', {
      method: 'POST',
      body: JSON.stringify({ playerId })
    });
    await refreshFriends();
    return r;
  }

  async function removeFriend(playerId) {
    const r = await api('/friends/' + encodeURIComponent(playerId), { method: 'DELETE' });
    await refreshFriends();
    return r;
  }

  function openTown() {
    cppEvent('panel_opened');
    DW.ui.show('town', townData());
    if (token) refreshFriends().catch(() => {});
  }

  function openAppearance() {
    cppEvent('panel_opened');
    const p = state.player || {};
    DW.ui.show('appearance', {
      signedIn: state.signedIn,
      appearance: p.appearance || {}
    });
  }

  function openPanel() {
    cppEvent('panel_opened');
    DW.ui.show('cloud', snapshot());
    if (panelOff) panelOff();
    panelOff = onChange(() => {
      const ov = document.getElementById('ov-cloud');
      if (!ov || !ov.classList.contains('open')) {
        if (panelOff) panelOff();
        panelOff = null;
        return;
      }
      DW.ui.show('cloud', snapshot());
    });
    if (token) {
      refresh().catch((e) => {
        state.error = e.message;
        if (e.status === 401) clearToken();
        emit();
      });
    }
    emit();
  }

  return {
    state: snapshot,
    onChange,
    setBase,
    login,
    register,
    logout,
    clearToken,
    refresh,
    connectWs,
    submitMission,
    openPanel,
    openTown,
    openAppearance,
    townData,
    refreshFriends,
    position,
    sendChat,
    saveAppearance,
    requestFriend,
    acceptFriend,
    declineFriend,
    removeFriend,
    hasToken: () => !!token
  };
})();

// The C++ world calls this to publish the local character's position.
DW.net = {
  position(x, y, mapId) {
    try { DW.cloud.position(x, y, mapId); } catch (e) {}
  }
};

// ---- the character's phone: the social device you carry ----
DW.phone = (() => {
  let app = 'home';
  function data() { return Object.assign({ app }, DW.cloud.townData()); }
  function isOpen() {
    const o = document.getElementById('ov-phone');
    return !!(o && o.classList.contains('open'));
  }
  function open() {
    if (isOpen()) { closePhone(); return; }
    app = 'home';
    cppEvent('panel_opened');
    DW.ui.show('phone', data());
    DW.cloud.refreshFriends().catch(() => {});
  }
  function closePhone() { DW.ui.hide('phone'); }
  function go(next) { app = next; DW.ui.show('phone', data()); }
  function paint() { if (isOpen()) DW.ui.paintPhone(data()); }
  return { open, close: closePhone, go, paint, isOpen, current: () => app };
})();

// ---- the machine in the apartment/lab: a desktop that launches the IDE ----
// The desktop is both a launcher for real C++ panels and a small OS of its
// own: a live terminal and a persistent scratchpad. Everything it prints comes
// from the same account/world state the rest of the game uses.
DW.computer = (() => {
  let app = 'desktop';
  const notesKey = 'devworld.notes.v1';
  const welcome = ['DEVWORLD OS 0.9 — NOVA-80 shell', 'Type "help" for commands.'];
  let lines = welcome.slice();

  function readNotes() {
    try { return localStorage.getItem(notesKey) || ''; } catch (e) { return ''; }
  }
  function profile() {
    try {
      return JSON.parse(Module.UTF8ToString(
        Module.ccall('dw_panel_json', 'number', ['string'], ['profile'])
      ));
    } catch (e) { return {}; }
  }
  function data() {
    return {
      profile: profile(),
      cloud: DW.cloud.townData(),
      app,
      lines,
      notes: readNotes()
    };
  }
  function isOpen() {
    const o = document.getElementById('ov-computer');
    return !!(o && o.classList.contains('open'));
  }
  function show() { DW.ui.show('computer', data()); }
  function open() {
    app = 'desktop';
    cppEvent('panel_opened');
    show();
    DW.cloud.refreshFriends().catch(() => {});
  }
  function desktop() { app = 'desktop'; show(); }
  function view(next) { app = next === 'notes' ? 'notes' : 'terminal'; show(); }
  function close() { DW.ui.hide('computer'); }
  function saveNotes(text) {
    try { localStorage.setItem(notesKey, String(text || '').slice(0, 20000)); } catch (e) {}
  }
  function push(text) { lines.push(text); lines = lines.slice(-80); }
  function run(raw) {
    const cmd = String(raw || '').trim();
    if (!cmd) return;
    push('> ' + cmd);
    const c = DW.cloud.townData();
    const me = c.me || {};
    const pr = c.progression || {};
    const low = cmd.toLowerCase();
    const open = (what) => {
      Module.ccall('dw_computer_action', null, ['string'], ['panel:' + what]);
    };
    if (low === 'help') {
      push('who · me · missions · friends · balance · status');
      push('chat · date · notes · clear · exit');
      push('open ide | board | town | shop | bank | map | settings');
    } else if (low === 'who') {
      const list = c.online || [];
      push(list.length ? list.length + ' other account(s) in town:' : 'No one else is in town.');
      list.forEach((o) => push('  ' + (o.displayName || o.handle || '?') + '  @' + (o.handle || '?')));
    } else if (low === 'me') {
      push((me.displayName || 'Unknown') + ' @' + (me.handle || '—'));
      push('  level ' + (pr.level || 1) + ' · ' + (pr.totalXp || 0) + ' XP · home #' + (me.homeUnit || '—'));
      push('  ' + Number(c.balance || 0).toLocaleString() + ' cr · ' + (me.completedMissions || []).length + ' missions shipped');
    } else if (low === 'missions') {
      const done = me.completedMissions || [];
      push(done.length + ' of ' + (c.missions || []).length + ' contracts shipped.');
      (c.missions || []).slice(0, 8).forEach((m) => push('  ' + (done.includes(m.id) ? '[x] ' : '[ ] ') + m.id + '  ' + m.title));
      if ((c.missions || []).length > 8) push('  … open the Mission Board for the rest.');
    } else if (low === 'friends') {
      const f = c.friends || [];
      push(f.length ? f.length + ' friend(s):' : 'No friends yet. Open People in the town panel.');
      f.forEach((o) => push('  ' + (o.displayName || o.handle || '?') + '  @' + (o.handle || '?')));
    } else if (low === 'balance') {
      push(Number(c.balance || 0).toLocaleString() + ' credits available.');
      push('Recent events:');
      (c.events || []).slice(0, 4).forEach((e) => push('  ' + (e.text || '')));
    } else if (low === 'status') {
      push('town socket: ' + (c.connected ? 'connected' : 'offline'));
      push('account: ' + (c.signedIn ? '@' + (me.handle || '—') : 'not signed in'));
      push('renderer: online · ledger: synced');
    } else if (low === 'chat') {
      const chat = (c.chat || []).slice(-5);
      if (!chat.length) push('No town chat yet.');
      chat.forEach((m) => push('  ' + ((m.from || {}).displayName || 'Town') + ': ' + (m.text || '')));
    } else if (low === 'date') {
      push(new Date().toString());
    } else if (low === 'notes') {
      push('Opening notes.txt…');
      app = 'notes';
    } else if (low === 'clear') {
      lines = welcome.slice();
    } else if (low === 'exit') {
      app = 'desktop';
    } else if (low === 'sudo') {
      push('nice try. this terminal is already root of its own little world.');
    } else if (low.startsWith('open ')) {
      const what = low.slice(5).trim();
      if (what === 'ide') {
        Module.ccall('dw_computer_action', null, ['string'], ['ide']);
        return;
      }
      if (what === 'town') {
        Module.ccall('dw_computer_action', null, ['string'], ['town']);
        DW.cloud.openTown();
        return;
      }
      if (what === 'board') {
        Module.ccall('dw_computer_action', null, ['string'], ['board']);
        return;
      }
      const panels = { shop: 'shop', bank: 'bank', map: 'worldmap', settings: 'settings', skills: 'skilltree', inventory: 'inventory' };
      if (panels[what]) { open(panels[what]); return; }
      push('No program named "' + what + '". Try help.');
    } else {
      push('command not found: ' + cmd + ' — type help.');
    }
    show();
  }
  return { open, close, desktop, app: view, command: run, saveNotes, isOpen, current: () => app };
})();

// A small always-on status chip; clicking it opens the cloud panel.
(function bootCloud() {
  function start() {
    const root = document.getElementById('ui-root');
    if (!root || !DW.cloud) return;
    let chip = document.getElementById('cloud-chip');
    if (!chip) {
      chip = document.createElement('button');
      chip.id = 'cloud-chip';
      chip.type = 'button';
      root.appendChild(chip);
    }
    chip.addEventListener('click', () => DW.cloud.openPanel());
    const paint = (s) => {
      chip.textContent = s.signedIn
        ? '☁ Lv ' + ((s.progression && s.progression.level) || 1)
        : '☁ Sign in';
      chip.classList.toggle('signed-in', !!s.signedIn);
      chip.classList.toggle('live', !!s.connected);
      chip.title = s.signedIn
        ? ((s.player && (s.player.displayName || s.player.handle)) || 'Signed in') +
          ' · ' + (s.connected ? 'live' : 'offline')
        : 'Sign in to the platform server';
      const town = document.getElementById('hud-town');
      if (town) {
        town.textContent = s.online > 0 ? 'Town · ' + s.online : 'Town';
        town.classList.toggle('live', !!s.connected);
      }
    };
    DW.cloud.onChange(paint);
    paint(DW.cloud.state());
    document.addEventListener('keydown', (e) => {
      const key = (e.key || '').toLowerCase();
      if ((key !== 't' && key !== 'f') || e.metaKey || e.ctrlKey || e.altKey) return;
      const tag = ((e.target && e.target.tagName) || '').toLowerCase();
      if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
      if (DW.ide && DW.ide.isOpen && DW.ide.isOpen()) return;
      const openOverlay = document.querySelector('.overlay.open');
      if (key === 't') {
        if (openOverlay && openOverlay.id !== 'ov-town') return;
        DW.cloud.openTown();
      } else {
        if (openOverlay && openOverlay.id !== 'ov-phone') return;
        DW.phone.open();
      }
    });
    if (DW.cloud.hasToken()) {
      DW.cloud.refresh()
        .then(() => DW.cloud.connectWs())
        .catch((e) => { if (e && e.status === 401) DW.cloud.clearToken(); });
    }
  }
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
})();

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
    dispatchInteractKey();
  }, { passive: false });

  // The touch button must honor remapping: read the *current* interact
  // keybind from the settings panel payload and press that key. Falls back
  // to E when the cache can't be populated (e.g. before first settings read).
  let interactKey = 69;
  function dispatchInteractKey() {
    try {
      const d = JSON.parse(Module.UTF8ToString(
        Module.ccall('dw_panel_json', 'number', ['string'], ['settings'])));
      const k = d && d.keybinds && d.keybinds.interact;
      if (typeof k === 'number' && k) interactKey = k;
    } catch (err) {}
    window.dispatchEvent(new KeyboardEvent('keydown', { keyCode: interactKey }));
  }
})();
