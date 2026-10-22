#include "progression.h"

#include <algorithm>

namespace dw {

int64_t xpForLevel(int level) {
  if (level < 1) level = 1;
  if (level >= kMaxLevel) return 0;
  return (int64_t)level * level * 100;
}

XpResult applyXp(LevelState& state, int64_t amount) {
  XpResult r;
  state.xp += amount;
  if (state.xp < 0) state.xp = 0;
  while (state.level < kMaxLevel) {
    int64_t need = xpForLevel(state.level);
    if (need <= 0 || state.xp < need) break;
    state.xp -= need;
    state.level++;
    r.levelsGained++;
  }
  r.level = state.level;
  r.xp = state.xp;
  return r;
}

XpResult levelFromTotal(int64_t totalXp) {
  LevelState s;
  return applyXp(s, totalXp);
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

JVal progressionView(int64_t totalXp) {
  LevelState s;
  applyXp(s, totalXp);
  JVal v = jobj();
  jset(v, "totalXp", jnum((double)totalXp));
  jset(v, "level", jnum(s.level));
  jset(v, "xpIntoLevel", jnum((double)s.xp));
  jset(v, "xpForNextLevel", jnum((double)xpForLevel(s.level)));
  jset(v, "title", jstr(titleForLevel(s.level)));
  return v;
}

bool requirementsMet(const MissionDef& mission, const std::set<std::string>& completed) {
  for (const auto& req : mission.requires)
    if (!completed.count(req)) return false;
  return true;
}

std::vector<std::string> eligibleUnlocks(const std::map<std::string, MissionDef>& missions,
                                         const std::set<std::string>& completed) {
  std::vector<std::string> out;
  for (const auto& kv : missions) {
    if (completed.count(kv.first)) continue;
    if (!requirementsMet(kv.second, completed)) continue;
    out.push_back(kv.first);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace dw
