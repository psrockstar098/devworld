#include "profile.h"
#include "progression.h"

#include <algorithm>

namespace dw {

std::string reputationTier(int64_t rep) {
  if (rep < 10) return "Newcomer";
  if (rep < 50) return "Trusted";
  if (rep < 150) return "Respected";
  if (rep < 400) return "Veteran";
  return "Legend";
}

ProfileSummary profileFor(const Player& player, int missionsComplete, int missionsActive) {
  ProfileSummary s;
  s.name = player.name;
  s.displayName = player.displayName;
  s.level = player.levels.level;
  s.xp = player.levels.xp;
  s.xpToNext = xpForLevel(player.levels.level);
  s.title = titleForLevel(player.levels.level);
  s.reputation = player.reputation;
  s.reputationTier = reputationTier(player.reputation);
  s.missionsComplete = missionsComplete;
  s.missionsActive = missionsActive;
  s.playMinutes = (int)(player.playSeconds / 60.0);
  s.achievements = player.achievements;

  for (const auto& a : player.achievements) {
    if (a.rfind("visited_", 0) == 0) s.visitedCities.push_back(a.substr(8));
  }
  std::sort(s.visitedCities.begin(), s.visitedCities.end());
  s.visitedCities.erase(std::unique(s.visitedCities.begin(), s.visitedCities.end()),
                        s.visitedCities.end());

  for (const auto& kv : player.equipped) s.loadout.push_back(kv.first + ":" + kv.second);
  std::sort(s.loadout.begin(), s.loadout.end());
  return s;
}

void pushNotice(std::vector<Notice>& log, const Notice& n, size_t cap) {
  log.push_back(n);
  if (log.size() > cap) log.erase(log.begin(), log.begin() + (log.size() - cap));
}

}  // namespace dw
