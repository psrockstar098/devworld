#pragma once
// achievements.h — the data-driven achievement system (objective 5.1/5.4).
//
// Server-authoritative copy (Phase 2): achievements are earned by capability,
// not by grinding, and the server decides what a player holds — the client
// never grants itself one. The shell keeps its own copy for offline play.
// Keep the two in sync when the model changes.
//
// An achievement is a named milestone the player earns by capability: each
// carries the same UnlockRequirement axes as the unlock catalog, so "why did
// I get this?" has a concrete answer and spam cannot earn one.
//
// Awards are idempotent — the same achievement never lands twice — and
// existing ad-hoc achievement ids (visited_<city>, level_<n>, dungeon ids)
// remain valid; the catalog simply adds a title and description for the ones
// it knows.
//
// Adaptation note: the shell's award mutates a shell Player. The server has
// no such record (PlayerRecord is an identity record), so awarding here
// takes the player's held-achievement list directly; persisting it to a
// record is API work, not model work.
//
// Pure: no SDL, no I/O, no clock. Design: docs/design/09-progression-engine.md.

#include <map>
#include <string>
#include <vector>

#include "capability.h"
#include "curriculum.h"
#include "json.h"
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

// Award every earned achievement the player does not already hold. Idempotent:
// `held` gains only the new ids, in catalog order. Returns the newly awarded
// ids in catalog order; *firstNew is the first one.
std::vector<std::string> awardAchievements(std::vector<std::string>& held,
                                           const AchievementCatalog& cat,
                                           const PlayerCapability& cap,
                                           const UnlockCatalog* ucat = nullptr,
                                           const Curriculum* cur = nullptr,
                                           std::string* firstNew = nullptr);

}  // namespace dw
