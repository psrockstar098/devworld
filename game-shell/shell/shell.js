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

  function applyA11y(d) {
    const root = document.documentElement;
    root.style.fontSize = (16 * (d.uiScale || 100) / 100) + 'px';
    root.style.setProperty('--text-scale', String((d.textScale || 100) / 100));
    root.classList.toggle('a11y-contrast', !!d.highContrast);
    root.classList.toggle('a11y-colorblind', !!d.colorblind);
    root.classList.toggle('reduced-motion', !!d.reducedMotion);
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
    document.getElementById('hud-quests').textContent = 'Quests: ' + d.quests;
    document.getElementById('hud-clock').textContent = d.clock;
    document.getElementById('hud-map').textContent = d.map;
    const track = document.getElementById('hud-track');
    if (track) {
      track.textContent = d.trackedTitle ? `▸ ${d.trackedTitle}: ${d.trackedObjective}` : '';
    }
  }

  // ---- panel renderers ----
  const R = {
    charcreate(d) {
      const skins = [[232,190,150],[210,170,130],[160,110,80],[240,200,160]];
      const shirts = [[45,212,191],[200,80,80],[80,160,200],[200,170,60],[140,120,200]];
      const pants = [[51,65,85],[30,30,40],[110,80,55],[70,90,60]];
      const hairc = [[45,35,28],[90,60,40],[180,140,90],[30,30,34],[150,150,150],[160,60,70]];
      const sw = (arr, key) => arr.map((c, i) =>
        `<button class="sw" data-k="${key}" data-i="${i}" style="background:rgb(${c.join(',')})"></button>`).join('');
      return `<h2>New Arrival</h2><p class="dim">The bus dropped you in Starter Town with a second-hand laptop and a head full of syntax. Who are you?</p>
        <label>Name <input id="cc-name" maxlength="16" value="Dev" /></label>
        <label>Display name <input id="cc-display" maxlength="16" value="Dev" /></label>
        <div class="swrow"><span>Skin</span>${sw(skins,'skin')}</div>
        <div class="swrow"><span>Hair</span>${sw(hairc,'hair')}</div>
        <div class="swrow"><span>Shirt</span>${sw(shirts,'shirt')}</div>
        <div class="swrow"><span>Pants</span>${sw(pants,'pants')}</div>
        <p class="dim">You can change none of this later. Choose like it matters. (It does, a little.)</p>
        <button class="primary" id="cc-go">Start life in Starter Town</button>`;
    },
    missionboard(d) {
      const stName = ['Locked','Available','Active','Submitted','Complete'];
      const rows = (d.missions || []).map((m) => {
        const st = stName[m.state] || '?';
        const gate = m.gate ? `<span class="tag gate">GATE · no hints</span>` : '';
        const btn = m.state === 1 ? `<button class="primary" data-act="accept" data-id="${esc(m.id)}">Accept</button>`
          : m.state === 2 ? `<span class="tag active">In progress — code it at a computer</span>`
          : m.state === 4 ? `<span class="tag done">Complete</span>`
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
      return `<h2>Level ${d.level} — ${esc(d.title || 'Developer')}</h2>
        <div class="xpbar big"><div class="xpfill" style="width:${pct}%"></div></div>
        <p>${d.xp} / ${d.need} XP · Reputation ${d.reputation}</p>
        <p class="dim">Skill trees track per-technology mastery through verified evidence. Phase 1: Python and SQL paths grow as you complete missions. Full trees arrive with the progression service (Phase 2).</p>`;
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
        <p class="dim">${d.totalUnread} unread · team ${teamSize}/${d.teamMax}. Phase 1 contacts are written characters, not people. [PHASE2] real friends and messaging.</p>
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
        <h3>Safety</h3><p class="dim">Block / report arrive with multiplayer (Phase 2).</p>`;
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
        <button class="opt" data-act="pportfolio">Project portfolio</button>
        <button class="opt" data-act="pcontacts">Contacts &amp; team</button>
        <button class="opt" data-act="pcompany">Company</button>
        <button class="opt" data-act="phome">Home &amp; decoration</button>
        <button class="opt" data-act="psettings">Settings</button>
        <button class="opt" data-act="psave">Save now</button>`;
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
    if (id === 'settings') wireSettings(data);
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
        else if (a === 'pportfolio') { close('all'); requestPanel('portfolio'); }
        else if (a === 'pcontacts') { close('all'); requestPanel('contacts'); }
        else if (a === 'pcompany') { close('all'); requestPanel('company'); }
        else if (a === 'phome') { close('all'); requestPanel('home'); }
        else if (a === 'psettings') { close('all'); requestPanel('settings'); }
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

  const CC = { skin: 0, shirt: 0, pants: 0, hair: 0 };
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
    const hairc = [[45,35,28],[90,60,40],[180,140,90],[30,30,34],[150,150,150],[160,60,70]];
    document.getElementById('cc-go').addEventListener('click', () => {
      send('char_done', {
        name: document.getElementById('cc-name').value || 'Dev',
        display: document.getElementById('cc-display').value || 'Dev',
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

  return { show, hide: close, notify, updateHud, setNpc, esc, request: requestPanel };
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
