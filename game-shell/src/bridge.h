#pragma once
// bridge.h — the C++ <-> page-JavaScript contract for the DEVWORLD game shell.
//
// The shell page (dist/index.html) implements these `dw_*` JS functions.
// The game canvas is SDL2; ALL panels/dialogs/HUD chrome are DOM (driven from
// here), and the coding workspace is the real VS Code IDE in an <iframe>.
//
// IDE bridge detail: the IDE lives same-origin in #ide-frame, so we reach it
// as `ideFrame.contentWindow.GameIDE`. Mission test cases are seeded by merging
// into the IDE's localStorage key `devworld.tests.v1` ({path: [{name, stdin,
// expected, hidden}]}), then `GameIDE.runTests()` runs them against the ACTIVE
// file and resolves {passed, total}. The detailed report (with diffs for
// visible failures; hidden tests shown only as PASS/FAIL) prints to the IDE's
// own output channel — the shell only ever sees the summary counts.

#include <string>
#include "types.h"

namespace dw {

// ---- UI panels (JS implements; C++ drives) ----
void ui_show(const std::string& panel, const std::string& json = "{}");
void ui_hide(const std::string& panel);
void ui_update_hud();                       // refresh XP bar, credits, clock, quest tracker
void notify(const std::string& text, const std::string& kind = "info");  // toast

// ---- IDE overlay ----
void ide_open();                            // show the IDE overlay panel
void ide_close();                           // hide it, back to town
bool ide_is_open();
// Seed a mission: write starter file, merge test cases into the IDE store,
// open the file. Safe to call repeatedly (idempotent per mission).
void ide_seed_mission(const MissionDef& m);
// Submit: ensures the mission file is active, runs tests, result arrives via
// dw_mission_result() (called from JS when the promise resolves).
void ide_submit_mission(const MissionDef& m);

// ---- persistence (localStorage devworld.save.v1) ----
std::string load_save();  // "" when none

// ---- in-game devices ----
void computer_open();  // show the machine's desktop in the page

// ---- shared world ----
// Publish this character's position (world px) on the shared map. No-op when
// the page is offline; the shell's net layer owns the socket.
void world_position(float x, float y, const std::string& mapId);

// ---- audio (WebAudio bleeps; ambient hooks) ----
void sfx(const std::string& name);  // "interact" | "success" | "error" | "coin" | "open" | "close"

// ---- misc ----
std::string iso_now();          // client clock ISO string (Phase 1)
int64_t now_ms();               // client clock epoch milliseconds (Phase 1)
std::string make_id();          // random id for ledger rows

}  // namespace dw
