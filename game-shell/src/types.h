#pragma once
// Shared types for the DEVWORLD game shell.
// Phase 1: Starter Town only. All multiplayer/server hooks are stubs marked
// [PHASE2] — the design (ARCH §5) makes the server authoritative for valuable
// state; the client never trusts itself for economy in the real design.

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "settings.h"
#include "mastery.h"

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

// Convenience-only item effects. The content validator pins this list:
// an item may never carry an effect that changes a test verdict.
struct ItemEffects {
  int xpPercent = 0;
  int concurrentMissions = 0;
  int buildSpeedPercent = 0;
  int comfort = 0;
};

struct ItemDef {
  std::string id, name, desc;
  int64_t price = 0;
  std::string slot;      // "computer" | "tool" | "consumable" | "decor"
  ItemEffects effects;   // convenience only (see equipment.h)
};

// ---- progression ----
struct LevelState {
  int level = 1;
  int64_t xp = 0;
  // XP math lives in progression.h (pure, unit-tested; gates are the real check).
};

// Raw mission-outcome counters (design 09 §5.1). The reliability *score* is
// DERIVED from these, never stored, so it cannot be inflated by cosmetics or
// by repeating trivial work. Append-only, like evidence.
struct ReliabilityRecord {
  int passes = 0;   // missions completed
  int fails = 0;    // failed test submissions
  int recalls = 0;  // delayed-recall practice passes
};

// The player's chosen specialization and its history (design 09 §5.5).
// Switching paths appends to `history` and never deletes evidence, completed
// missions or achievements — earned competencies survive respecialization.
// `milestones` maps a milestone id (see content/unlocks.json) to the ISO time
// it was earned.
struct CareerRecord {
  std::string path;                               // "" = no path chosen yet
  std::vector<std::string> history;               // every path held, oldest first
  std::map<std::string, std::string> milestones;  // milestone id -> ISO time
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
  // Campaign metadata (Phase 1 = levels 1-5). Gates are independent
  // assessments: no hints, hidden tests only (design: 04-progression).
  int level = 1;
  bool gate = false;
  bool hints = true;
  // Independent assessments emit transfer/rubric evidence: the only evidence
  // that can make a skill "independent" and therefore "mastered" (design 08
  // §4.3). Gates are always independent; a capstone proof can be flagged too.
  bool independent = false;
};
enum class MissionState { Locked, Available, Active, Submitted, Complete };
struct MissionProgress {
  MissionState state = MissionState::Locked;
  int attempts = 0;
};

struct Notice {
  std::string type;  // "info" | "success" | "quest" | "error" | "levelup"
  std::string text;
  std::string at;    // ISO time
};

// ---- social ----
struct Message {
  std::string contactId;
  bool fromThem = false;  // true = received, false = sent
  std::string text;
  std::string at;
};

// ---- company ----
struct CompanyState {
  bool founded = false;
  std::string name, focus;
  int reputation = 0;
  int contracts = 0;
  std::vector<std::string> members;  // contact ids on the roster
};

// ---- player ----
struct Player {
  std::string name = "Dev";
  std::string displayName = "Dev";
  Color skin{232, 190, 150}, shirt{45, 212, 191}, pants{51, 65, 85};
  Color hair{45, 35, 28};       // character customization
  Vec2 pos{0, 0};          // world px
  std::string mapId = "town";  // "town" | interior id
  Location loc;
  LevelState levels;
  int64_t reputation = 0;
  std::vector<std::string> achievements;
  std::map<std::string, int> inventory;  // itemId -> qty
  std::map<std::string, std::string> equipped;  // slot -> itemId
  std::vector<LedgerRow> ledger;
  std::map<std::string, MissionProgress> missions;  // missionId -> progress
  std::map<std::string, std::string> flags;         // npc/quest flags
  std::string trackedMission;                       // objective tracker
  std::vector<Notice> notices;                      // newest last, capped
  std::map<std::string, std::string> homeLayout;    // home slot id -> item id
  std::vector<std::string> homeTrophies;            // achievements on display
  std::vector<Message> messages;                    // newest last
  std::map<std::string, int> unread;                // contactId -> unread count
  std::vector<std::string> team;                    // contact ids in your party
  CompanyState company;
  AccessibilitySettings a11y;
  double playSeconds = 0;                           // session-persisted playtime
  // Progression records kept deliberately separate from level (design 09 §5.1):
  // a high level must not imply technical expertise. Reliability is raw
  // counters; the career is the chosen path plus everything ever held.
  ReliabilityRecord reliability;
  CareerRecord career;
  // Education: evidence-based mastery (design 08 §4.3). Append-only; mastery
  // and skill-tree state are DERIVED from these rows, never stored twice.
  // [PHASE2] evidence is server-issued; the client cache is untrusted.
  std::vector<Evidence> evidence;
  int64_t balance() const {
    int64_t b = 0;
    for (auto& r : ledger) b += r.delta;
    return b;
  }
};

// ---- other real players ----
// A live snapshot of someone else's character in the shared town. Positions
// come from the server's presence registry (world px), never from local guess;
// this struct is only a render cache.
struct RemotePlayer {
  std::string id, handle, displayName, mapId = "town";
  Color skin{232, 190, 150}, shirt{45, 212, 191}, pants{51, 65, 85};
  Color hair{45, 35, 28};
  Vec2 pos{0, 0};
  int facing = 0;
  bool moving = false;
  float movingT = 0;   // seconds left of the walk animation
  float phase = 0;
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
  std::string id, label, kind;  // "door" | "computer" | "board" | "npc" | "shop"
                                // | "bank" | "bus" | "mail" | "inspect"
  std::string mapId;
  Rect rect;                    // world px
  std::string target;           // door target mapId, or npc id, etc.
  std::string text;             // lore for "inspect" objects
};

}  // namespace dw
