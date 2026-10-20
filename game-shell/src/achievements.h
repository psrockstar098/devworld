#pragma once
// achievements.h — the data-driven achievement system (objective 5.1/5.4).
//
// An achievement is a named milestone the player earns by capability, not by
// grinding: each carries the same UnlockRequirement axes as the unlock catalog,
// so "why did I get this?" has a concrete answer and spam cannot earn one.
//
// Awards are idempotent — the same achievement never lands twice — and existing
// ad-hoc achievement ids (visited_<city>, level_<n>, dungeon ids) remain valid;
// the catalog simply adds a title and description for the ones it knows.
//
// Pure: no SDL, no I/O, no clock. Design: docs/design/09-progression-engine.md.

#include <map>
#include <string>
#include <vector>

#include "capability.h"
#include "curriculum.h"
#include "json.h"
#include "types.h"
#include "unlocks.h"

namespace dw {

struct AchievementDef {
  std::string id, title, desc;
  bool hidden = false;   // shown as "???" until earned
  UnlockRequirement requires;
};

class AchievementCatalog {
 public:
  void load(const JVal& catalogJson);  // content/achievements.json
  bool empty() const { return defs_.empty(); }
  const std::vector<AchievementDef>& all() const { return defs_; }
  const AchievementDef* def(const std::string& id) const;

  // Ids the capability satisfies, in catalog order.
  std::vector<std::string> earned(const PlayerCapability& cap, const UnlockCatalog* cat = nullptr,
                                  const Curriculum* cur = nullptr) const;

  // "Why not yet" lines for one achievement.
  std::vector<std::string> progress(const AchievementDef& d, const PlayerCapability& cap,
                                    const UnlockCatalog* cat = nullptr,
                                    const Curriculum* cur = nullptr) const;

  // Unique ids, non-empty titles, non-empty requirement sets (a free
  // achievement is not an achievement).
  std::vector<std::string> validate() const;

 private:
  std::vector<AchievementDef> defs_;
  std::map<std::string, size_t> byId_;
};

// Award every earned achievement the player does not already hold. Idempotent.
// Returns the newly awarded ids in catalog order; *firstNew is the first one.
std::vector<std::string> awardAchievements(Player& player, const AchievementCatalog& cat,
                                           const PlayerCapability& cap,
                                           const UnlockCatalog* ucat = nullptr,
                                           const Curriculum* cur = nullptr,
                                           std::string* firstNew = nullptr);

}  // namespace dw
