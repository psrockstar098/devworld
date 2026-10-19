#pragma once
// progression.h — pure progression + mission-gating rules.
//
// Deliberately free of SDL, Emscripten and I/O so the rules can be unit-tested
// natively (g++) and in WebAssembly (em++) with the same code the game runs.
// Game::addXp / Game::completeMission are thin wrappers around these functions.
// Design: docs/design/04-progression-50-levels.md, 02-game-design-document.md §2.

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

// Highest level in the design. XP keeps accumulating at the cap.
constexpr int kMaxLevel = 50;

// XP required to advance from `level` to `level + 1`.
// Illustrative Phase 1 curve; gates (not the curve) are the real checks.
int64_t xpForLevel(int level);

struct XpResult {
  int levelsGained = 0;
  int level = 1;        // resulting level
  int64_t xp = 0;       // XP remainder toward the next level
};

// Applies XP, levelling up as many times as earned. Never loops forever and
// never exceeds kMaxLevel. Pure: no notifications, no save, no SDL.
XpResult applyXp(LevelState& levels, int64_t amount);

// True when every mission in `requires` is Complete.
bool requirementsMet(const MissionDef& mission,
                     const std::map<std::string, MissionProgress>& progress);

// All currently-Locked missions whose requirements are met, sorted by id.
// Locked -> Available is the only transition this returns.
std::vector<std::string> eligibleUnlocks(
    const std::map<std::string, MissionDef>& missions,
    const std::map<std::string, MissionProgress>& progress);

// Number of missions in Active or Submitted state (for the HUD).
int countActiveMissions(const std::map<std::string, MissionProgress>& progress);

// Tier title from the 50-level spec.
std::string titleForLevel(int level);

// One-line objective for the quest log / tracker, derived from state.
std::string objectiveFor(const MissionDef& mission, const MissionProgress& progress);

}  // namespace dw
