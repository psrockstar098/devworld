#pragma once
// Shared types for the DEVWORLD game shell.
// Phase 1: Starter Town only. All multiplayer/server hooks are stubs marked
// [PHASE2] — the design (ARCH §5) makes the server authoritative for valuable
// state; the client never trusts itself for economy in the real design.

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace dw {

// ---- math ----
struct Vec2 { float x = 0, y = 0; };
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
inline bool rectsOverlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

// ---- color ----
struct Color { uint8_t r, g, b, a = 255; };

// ---- game state ----
enum class Screen {
  CharCreate,   // first-launch character creation (DOM panel)
  World,        // walking around
  // All panels below are DOM overlays; the world keeps rendering behind them.
};

// Where the player is. Phase 1: single-player instances.
// [PHASE2] regionId/instanceId become server-assigned; interiors are instanced
// per player/party on the world-sim service (ARCH §5).
struct Location {
  std::string regionId = "starter_town";  // e.g. "starter_town", "apt_interior"
  std::string instanceId = "solo";        // [PHASE2] server-assigned instance
};

// ---- economy ----
// Double-entry ledger (GDD §6): every money movement is a ledger row;
// balances are DERIVED, never stored. Phase 1 keeps the ledger in localStorage.
// [PHASE2] the economy service owns the ledger; the client only displays
// server-signed balances and submits idempotent intents (POST /shop/buy).
struct LedgerRow {
  std::string id;        // ulid-ish
  std::string ts;        // iso time (client clock, Phase 1)
  std::string kind;      // "mission_reward" | "shop_buy" | "shop_sell" | ...
  std::string memo;
  int64_t delta = 0;     // +credit / -debit, in credits
};

struct ItemDef {
  std::string id, name, desc;
  int64_t price = 0;
  std::string slot;      // "computer" | "tool" | "consumable" | "decor"
};

// ---- progression ----
struct LevelState {
  int level = 1;
  int64_t xp = 0;
  // XP curve: illustrative for Phase 1 (design: 04-progression, gates are real).
  static int64_t xpForLevel(int lvl) { return (int64_t)lvl * lvl * 100; }
};

// ---- quests ----
struct TestCase {
  std::string name, tstdin, expected;  // tstdin: `stdin` is a stdio.h macro
  bool hidden = false;
};
struct MissionDef {
  std::string id, title, giver;
  std::string briefing;
  std::string language;          // "python" | "sql"
  std::string file;              // path inside the IDE, e.g. missions/m01_hello.py
  std::string starter;           // starter code
  std::vector<TestCase> tests;
  std::vector<std::string> requires;  // mission ids that must complete first
  int64_t xpReward = 0;
  int64_t creditReward = 0;
};
enum class MissionState { Locked, Available, Active, Submitted, Complete };
struct MissionProgress {
  MissionState state = MissionState::Locked;
  int attempts = 0;
};

// ---- player ----
struct Player {
  std::string name = "Dev";
  std::string displayName = "Dev";
  Color skin{232, 190, 150}, shirt{45, 212, 191}, pants{51, 65, 85};
  Vec2 pos{0, 0};          // world px
  std::string mapId = "town";  // "town" | interior id
  Location loc;
  LevelState levels;
  int64_t reputation = 0;
  std::vector<std::string> achievements;
  std::map<std::string, int> inventory;  // itemId -> qty
  std::vector<LedgerRow> ledger;
  std::map<std::string, MissionProgress> missions;  // missionId -> progress
  std::map<std::string, std::string> flags;         // npc/quest flags
  int64_t balance() const {
    int64_t b = 0;
    for (auto& r : ledger) b += r.delta;
    return b;
  }
};

// ---- NPCs ----
struct DialogueOption {
  std::string text;
  std::string next;        // node id, or "@close", or "@mission:<id>"
  std::string requires;    // flag expression, "" = always
  std::string sets;        // flag to set when chosen
};
struct DialogueNode {
  std::string id, speaker, text;
  std::vector<DialogueOption> options;
};
struct NpcScheduleEntry {
  int fromHour = 0, toHour = 24;  // game hours
  std::string mapId;
  float x = 0, y = 0;             // tile coords
};
struct NpcDef {
  std::string id, name, job;
  Color shirt{100, 116, 139}, skin{232, 190, 150};
  std::string rootNode;
  std::map<std::string, DialogueNode> nodes;
  std::vector<NpcScheduleEntry> schedule;
};

// ---- interaction ----
struct Interactable {
  std::string id, label, kind;  // "door" | "computer" | "board" | "npc" | "shop" | "bank" | "bus"
  std::string mapId;
  Rect rect;                    // world px
  std::string target;           // door target mapId, or npc id, etc.
};

}  // namespace dw
