// progression.cpp — pure progression rules (no SDL, no Emscripten, no I/O).
#include "progression.h"

#include <algorithm>

namespace dw {

int64_t xpForLevel(int level) {
  if (level < 1) level = 1;
  if (level >= kMaxLevel) return 0;  // cap: no further level exists
  return (int64_t)level * level * 100;
}

XpResult applyXp(LevelState& levels, int64_t amount) {
  XpResult r;
  levels.xp += amount;
  if (levels.xp < 0) levels.xp = 0;  // XP never goes negative; no de-levelling
  while (levels.level < kMaxLevel) {
    int64_t need = xpForLevel(levels.level);
    if (need <= 0 || levels.xp < need) break;
    levels.xp -= need;
    levels.level++;
    r.levelsGained++;
  }
  r.level = levels.level;
  r.xp = levels.xp;
  return r;
}

bool requirementsMet(const MissionDef& mission,
                     const std::map<std::string, MissionProgress>& progress) {
  for (const auto& req : mission.requires) {
    auto it = progress.find(req);
    if (it == progress.end() || it->second.state != MissionState::Complete)
      return false;
  }
  return true;
}

std::vector<std::string> eligibleUnlocks(
    const std::map<std::string, MissionDef>& missions,
    const std::map<std::string, MissionProgress>& progress) {
  std::vector<std::string> out;
  for (const auto& kv : missions) {
    auto pi = progress.find(kv.first);
    if (pi != progress.end() && pi->second.state != MissionState::Locked) continue;
    if (!requirementsMet(kv.second, progress)) continue;
    out.push_back(kv.first);
  }
  std::sort(out.begin(), out.end());
  return out;
}

int countActiveMissions(const std::map<std::string, MissionProgress>& progress) {
  int n = 0;
  for (const auto& kv : progress) {
    if (kv.second.state == MissionState::Active ||
        kv.second.state == MissionState::Submitted)
      n++;
  }
  return n;
}

std::string objectiveFor(const MissionDef& mission, const MissionProgress& progress) {
  switch (progress.state) {
    case MissionState::Complete:
      return "Proof of Skill recorded — " + mission.title;
    case MissionState::Submitted:
      return "Tests are running on " + mission.id + "…";
    case MissionState::Active:
      return "Open " + mission.file + " at a computer, write the solution, then run the tests"
             " (hidden cases decide).";
    case MissionState::Available:
      return "Accept at the mission board";
    case MissionState::Locked:
    default:
      if (!mission.requires.empty()) return "Locked — finish " + mission.requires.front() + " first";
      return "Locked";
  }
}

std::string titleForLevel(int level) {
  if (level < 1) level = 1;
  if (level <= 5) return "Beginner";
  if (level <= 10) return "Builder";
  if (level <= 15) return "Engineer";
  if (level <= 20) return "Specialist";
  if (level <= 25) return "Systems Developer";
  if (level <= 30) return "Inventor";
  if (level <= 35) return "Architect";
  if (level <= 40) return "Innovator";
  if (level <= 45) return "Master Engineer";
  return "Creator";
}

}  // namespace dw
