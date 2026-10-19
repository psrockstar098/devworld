// main.cpp — DEVWORLD game shell entry point (Emscripten + SDL2).
#include "game.h"
#include <emscripten.h>

using namespace dw;

static void loop_trampoline() {
  if (g_game) g_game->frame();
}

int main() {
  g_game = new Game();
  // Canvas fills the window; the shell page sizes it.
  if (!g_game->init(960, 600)) return 1;
  emscripten_set_main_loop(loop_trampoline, 0, 1);
  return 0;
}

// ---- C exports for shell.js (ccall) ----
extern "C" {

EMSCRIPTEN_KEEPALIVE
void dw_ui_event(const char* json) {
  if (g_game) g_game->uiEvent(json ? json : "{}");
}

EMSCRIPTEN_KEEPALIVE
void dw_mission_result(int passed, int total) {
  if (g_game) g_game->missionTestResult(passed, total);
}

EMSCRIPTEN_KEEPALIVE
void dw_set_keybind(const char* action, int keycode) {
  if (g_game) g_game->setKeybind(action ? action : "", keycode);
}

EMSCRIPTEN_KEEPALIVE
void dw_touch(const char* dir, int down) {
  if (g_game) g_game->touchMove(dir ? dir : "", down != 0);
}

EMSCRIPTEN_KEEPALIVE
void dw_ide_ready() {
  if (g_game) g_game->ideReady();
}

// Generic panel JSON (for shell.js to pull e.g. skilltree on demand).
static std::string panelBuf;
EMSCRIPTEN_KEEPALIVE
const char* dw_panel_json(const char* panel) {
  panelBuf = g_game ? g_game->panelJson(panel ? panel : "") : "{}";
  return panelBuf.c_str();
}

// Resize the SDL canvas when the window changes.
EMSCRIPTEN_KEEPALIVE
void dw_resize(int w, int h) {
  // (Phase 1: canvas is fixed by CSS; the renderer scales. Kept for Phase 2.)
  (void)w; (void)h;
}

}  // extern "C"
