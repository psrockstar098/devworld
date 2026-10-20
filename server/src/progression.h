#pragma once
// progression.h — server-side XP rules. Deliberately identical to
// game-shell/src/progression.cpp (the tests pin the equivalence).
#include <cstdint>
#include <string>

#include "json.h"

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

}  // namespace dw
