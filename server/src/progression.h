#pragma once
// progression.h — server-side XP rules. Deliberately identical to
// game-shell/src/progression.cpp (the tests pin the equivalence).
//
// The XP curve half (xpForLevel / applyXp / levelFromTotal / titleForLevel /
// progressionView) is a direct port of the shell's progression rules. The
// mission-gating half is adapted: the shell tracks a per-mission state
// machine (Locked / Available / Active / Submitted / Complete) and this
// server does not — it tracks the completed-mission set (capability /
// ledger). So requirementsMet and eligibleUnlocks take the completed set
// instead of a state map, and the two state-dependent helpers
// (countActiveMissions, objectiveFor) are shell-only until the server tracks
// mission states.
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "json.h"
#include "missions.h"

namespace dw {

constexpr int kMaxLevel = 50;

int64_t xpForLevel(int level);

struct LevelState {
  int level = 1;
  int64_t xp = 0;
};

struct XpResult {
  int levelsGained = 0;
  int level = 1;
  int64_t xp = 0;
};

XpResult applyXp(LevelState& state, int64_t amount);
XpResult levelFromTotal(int64_t totalXp);
std::string titleForLevel(int level);
JVal progressionView(int64_t totalXp);

// True when every mission in `requires` is in the completed set.
bool requirementsMet(const MissionDef& mission, const std::set<std::string>& completed);

// Missions the player can start now: requirements met, not yet completed,
// sorted by id. (Shell equivalent: Locked -> Available transitions.)
std::vector<std::string> eligibleUnlocks(const std::map<std::string, MissionDef>& missions,
                                         const std::set<std::string>& completed);

}  // namespace dw
