// bridge.cpp — EM_JS implementations of the C++<->JS contract.
// All functions call into the shell page's `window.DW` namespace (defined in
// shell/shell.js). No game logic lives here; this is plumbing.

#include "bridge.h"
#include "game.h"
#include "util.h"
#include <emscripten.h>

namespace dw {

EM_JS(void, dw_ui_show, (const char* panel, const char* json), {
  window.DW.ui.show(UTF8ToString(panel), JSON.parse(UTF8ToString(json)));
});
EM_JS(void, dw_ui_hide, (const char* panel), {
  window.DW.ui.hide(UTF8ToString(panel));
});
EM_JS(void, dw_ui_update_hud, (), { window.DW.ui.updateHud(); });
EM_JS(void, dw_notify, (const char* text, const char* kind), {
  window.DW.ui.notify(UTF8ToString(text), UTF8ToString(kind));
});

EM_JS(void, dw_ide_open, (), { window.DW.ide.open(); });
EM_JS(void, dw_ide_close, (), { window.DW.ide.close(); });
EM_JS(bool, dw_ide_is_open, (), { return window.DW.ide.isOpen(); });

// Seed a mission into the IDE: write starter code, merge test cases into the
// IDE's own test-case store (devworld.tests.v1), open the file.
// We merge (not overwrite) so the player's own test cases are never clobbered.
// Returns false when the IDE isn't loaded yet (caller queues a retry).
EM_JS(bool, dw_ide_seed, (const char* file, const char* code, const char* casesJson), {
  const f = UTF8ToString(file), c = UTF8ToString(code);
  const cases = JSON.parse(UTF8ToString(casesJson));
  const G = window.DW.ide.gameIDE();
  if (!G) return false;
  G.writeFile(f, c);
  const KEY = 'devworld.tests.v1';
  let store = {};
  try {
    const ls = window.DW.ide.frame().contentWindow.localStorage;
    store = JSON.parse(ls.getItem(KEY) || '{}') || {};
  } catch (e) {}
  store[f] = cases;
  try {
    window.DW.ide.frame().contentWindow.localStorage.setItem(KEY, JSON.stringify(store));
  } catch (e) {}
  G.openFile(f);
  return true;
});

// Submit: make sure the mission file is the active editor, then run the IDE's
// own test path. Result -> window.DW.onIdeTestsDone({passed,total,missionId}).
EM_JS(void, dw_ide_submit, (const char* file, const char* missionId), {
  const f = UTF8ToString(file), mid = UTF8ToString(missionId);
  const G = window.DW.ide.gameIDE();
  if (!G) { window.DW.ui.notify('IDE not ready yet.', 'error'); return; }
  G.openFile(f).then(() => G.runTests()).then((r) => {
    window.DW.onIdeTestsDone({ missionId: mid, passed: r.passed | 0, total: r.total | 0 });
  }).catch((e) => {
    window.DW.ui.notify('Test run failed: ' + (e && e.message || e), 'error');
  });
});

EM_JS(const char*, dw_load_save, (), {
  try { const s = localStorage.getItem('devworld.save.v1'); return s ? s : ""; }
  catch (e) { return ""; }
});
EM_JS(void, dw_sfx, (const char* name), { window.DW.audio.beep(UTF8ToString(name)); });
EM_JS(const char*, dw_iso_now, (), { return new Date().toISOString(); });
EM_JS(const char*, dw_make_id, (), {
  return 'id-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
});

// std::string wrappers (EM_JS returns UTF-8; copy into std::string immediately).
static std::string s_load_save() { const char* p = dw_load_save(); return p ? p : ""; }
static std::string s_iso_now() { const char* p = dw_iso_now(); return p ? p : ""; }
static std::string s_make_id() { const char* p = dw_make_id(); return p ? p : ""; }

void ui_show(const std::string& panel, const std::string& json) { dw_ui_show(panel.c_str(), json.c_str()); }
void ui_hide(const std::string& panel) { dw_ui_hide(panel.c_str()); }
void ui_update_hud() { dw_ui_update_hud(); }
void notify(const std::string& text, const std::string& kind) { dw_notify(text.c_str(), kind.c_str()); }

void ide_open() { dw_ide_open(); }
void ide_close() { dw_ide_close(); }
bool ide_is_open() { return dw_ide_is_open(); }
void ide_seed_mission(const MissionDef& m) {
  std::string cases = "[";
  for (size_t i = 0; i < m.tests.size(); i++) {
    if (i) cases += ",";
    const auto& t = m.tests[i];
    cases += "{\"name\":" + json_escape(t.name) +
             ",\"stdin\":" + json_escape(t.tstdin) +
             ",\"expected\":" + json_escape(t.expected) +
             ",\"hidden\":" + (t.hidden ? "true" : "false") + "}";
  }
  cases += "]";
  if (!dw_ide_seed(m.file.c_str(), m.starter.c_str(), cases.c_str())) {
    // IDE frame not loaded yet — queue; dw_ide_ready() flushes on iframe load.
    if (g_game) g_game->queueSeed(m.id);
  }
}
void ide_submit_mission(const MissionDef& m) { dw_ide_submit(m.file.c_str(), m.id.c_str()); }

std::string load_save() { return s_load_save(); }
void sfx(const std::string& name) { dw_sfx(name.c_str()); }
std::string iso_now() { return s_iso_now(); }
std::string make_id() { return s_make_id(); }

}  // namespace dw
