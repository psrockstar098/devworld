#pragma once
// profile.h — pure player-profile summary + notice log rules.
//
// The profile panel, the HUD and the quest log all read from one derived
// summary instead of inventing their own numbers. No SDL, no I/O.

#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct ProfileSummary {
  std::string name, displayName, title, reputationTier;
  int level = 1;
  int64_t xp = 0, xpToNext = 0, reputation = 0;
  int missionsComplete = 0, missionsActive = 0;
  int playMinutes = 0;
  std::vector<std::string> achievements;
  std::vector<std::string> visitedCities;  // from visited_* achievements
  std::vector<std::string> loadout;        // "slot:itemId", sorted by slot
};

// Derives the public profile from player state. Mission counts are passed in
// so the caller can use the same progress map it renders from.
ProfileSummary profileFor(const Player& player, int missionsComplete, int missionsActive);

// Reputation tiers (GDD §1): the town's trust in you, separate from level.
std::string reputationTier(int64_t reputation);

// Appends a notice, trimming the oldest so the log stays bounded.
void pushNotice(std::vector<Notice>& log, const Notice& n, size_t cap = 50);

}  // namespace dw
