// Browser end-to-end test for the DEVWORLD cloud panel.
//
// Prereqs:
//   cd game-shell/dist && python3 -m http.server 8000
//   google-chrome --headless=new --remote-debugging-port=9222 \
//     --no-sandbox --disable-gpu --user-data-dir=/tmp/devworld-chrome about:blank
//   server running on :8091
//
// Run: node server/tests/ui_test.mjs
//
// It drives the real page over the Chrome DevTools Protocol: opens the account
// panel, registers a user through the form, checks live WebSocket state and
// server-owned profile data, signs out, exercises a rejected password, and
// signs back in. No game state is faked; every value comes from the C++ server.

import fs from 'node:fs';

const CDP = process.env.CDP || 'http://127.0.0.1:9222';
const PAGE = process.env.PAGE || 'http://127.0.0.1:8000/';
const SERVER = process.env.SERVER || 'http://127.0.0.1:8091';

const target = await (
  await fetch(`${CDP}/json/new?${encodeURIComponent(PAGE)}`, { method: 'PUT' })
).json();
const ws = new WebSocket(target.webSocketDebuggerUrl);
let seq = 0;
const pending = new Map();
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) {
    pending.get(m.id)(m);
    pending.delete(m.id);
  }
};
await new Promise((r) => { ws.onopen = r; });

function send(method, params = {}) {
  return new Promise((resolve) => {
    const id = ++seq;
    pending.set(id, resolve);
    ws.send(JSON.stringify({ id, method, params }));
  });
}
async function ev(expression) {
  const r = await send('Runtime.evaluate', {
    expression, awaitPromise: true, returnByValue: true
  });
  if (r.result && r.result.exceptionDetails) {
    throw new Error(JSON.stringify(r.result.exceptionDetails.exception || r.result.exceptionDetails));
  }
  return r.result.result.value;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const results = [];
function check(name, ok, detail = '') {
  results.push({ name, ok });
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

await send('Page.enable');
await send('Runtime.enable');
await sleep(4000);

// Chrome keeps localStorage across runs in the same profile; start clean.
await ev(`localStorage.clear(); true`);
await send('Page.reload');
await sleep(4000);

check('page loaded', await ev(`document.readyState === 'complete'`), await ev('document.title'));
check('cloud chip rendered', await ev(`!!document.getElementById('cloud-chip')`), await ev(`document.getElementById('cloud-chip')?.textContent`));
check('chip starts signed out', await ev(`!document.getElementById('cloud-chip').classList.contains('signed-in')`));

await ev(`document.getElementById('cloud-chip').click()`);
await sleep(400);
check('cloud panel opens', await ev(`!!document.querySelector('#ov-cloud.open')`));
check('sign-in form present', await ev(`!!document.getElementById('cloud-login') && !!document.getElementById('cloud-user')`));

const user = 'ui' + Date.now().toString(36);
await ev(`(() => {
  document.getElementById('cloud-user').value = ${JSON.stringify(user)};
  document.getElementById('cloud-pass').value = 'hunter2hunter2';
  document.getElementById('cloud-display').value = 'UI Tester';
  document.getElementById('cloud-register').click();
  return true;
})()`);
await sleep(2500);

check('register signs in', await ev(`document.getElementById('cloud-chip').classList.contains('signed-in')`), await ev(`document.getElementById('cloud-chip').textContent`));
const head = await ev(`document.querySelector('#ov-cloud.open .cloud-head b')?.textContent || ''`);
check('profile name from server', head === 'UI Tester', head);
const stats = await ev(`Array.from(document.querySelectorAll('#ov-cloud.open .cloud-stats b')).map(b => b.textContent)`);
check('live stats rendered', Array.isArray(stats) && stats.length === 4, JSON.stringify(stats));
check('websocket live indicator', await ev(`document.getElementById('cloud-chip').classList.contains('live')`));
check('token persisted in localStorage', await ev(`!!localStorage.getItem('devworld.token')`));

const token = await ev(`localStorage.getItem('devworld.token')`);
const me = await (await fetch(`${SERVER}/me`, { headers: { Authorization: 'Bearer ' + token } })).json();
check('server /me matches browser user', me.handle === user || me.username === user, `handle=${me.handle} level=${me.progression?.level} credits=${me.balance}`);
check('password hash not exposed', !('passwordHash' in me));
const health = await (await fetch(`${SERVER}/health`)).json();
check('server counts the live socket', health.websockets >= 1, `websockets=${health.websockets}`);

// Sign out returns to the form, then a wrong password surfaces the server error.
await ev(`document.getElementById('cloud-logout')?.click()`);
await sleep(700);
check('sign out clears chip', await ev(`!document.getElementById('cloud-chip').classList.contains('signed-in')`));
check('sign out removes token', await ev(`!localStorage.getItem('devworld.token')`));
check('sign out shows the form again', await ev(`!!document.querySelector('#ov-cloud.open #cloud-login')`));

await ev(`(() => {
  document.getElementById('cloud-user').value = ${JSON.stringify(user)};
  document.getElementById('cloud-pass').value = 'definitely-wrong';
  document.getElementById('cloud-login').click();
  return true;
})()`);
await sleep(1500);
const err = await ev(`document.querySelector('#ov-cloud.open .cloud-err')?.textContent || ''`);
check('wrong password shows server error', /invalid|password/i.test(err), err);
check('failed login keeps user signed out', await ev(`!document.getElementById('cloud-chip').classList.contains('signed-in')`));

// Sign back in through the UI.
await ev(`(() => {
  document.getElementById('cloud-user').value = ${JSON.stringify(user)};
  document.getElementById('cloud-pass').value = 'hunter2hunter2';
  document.getElementById('cloud-login').click();
  return true;
})()`);
await sleep(2200);
check('sign in again works', await ev(`document.getElementById('cloud-chip').classList.contains('signed-in')`));
check('socket reconnects after sign in', await ev(`document.getElementById('cloud-chip').classList.contains('live')`));

if (process.env.SHOT) {
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(process.env.SHOT, Buffer.from(shot.result.data, 'base64'));
  check('screenshot captured', fs.statSync(process.env.SHOT).size > 1000);
}

const failed = results.filter((r) => !r.ok).length;
console.log(`\n${results.length - failed}/${results.length} checks passed`);
console.log('USER=' + user);
ws.close();
process.exit(failed ? 1 : 0);
