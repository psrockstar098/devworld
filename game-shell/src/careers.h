#pragma once
// careers.h — career specialization and respecialization (objective 5.5).
//
// A career path is a specialization the player chooses once its entry
// requirements are met. Entry requirements use the same separate axes as every
// other unlock, so each path matches a different set of strengths and "a player
// should not need to master an unrelated technology to access a career that
// matches their strengths" (objective 5.3).
//
// Choosing a path never deletes anything: evidence, completed missions and
// achievements stay, and `career.history` records every path ever held, so a
// respecialization keeps all earned competencies.
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

struct LadderRung {
  std::string kind;  // one of ladderRungKinds(), in that order
  std::string name;
  std::string blurb;
};

struct CareerDef {
  std::string id, title, language, blurb;
  UnlockRequirement entry;
  // The §6.5 project ladder: six rungs from guided exercise to capstone.
  // A path with no ladder is not invalid (older content), but every
  // shipped path carries one.
  std::vector<LadderRung> ladder;
};

// The six project-ladder rungs, in order (design 13 §6.5).
inline const std::vector<std::string>& ladderRungKinds() {
  static const std::vector<std::string> kKinds = {
    "guided", "component", "multi-concept", "spec", "team", "capstone",
  };
  return kKinds;
}

class Careers {
 public:
  void load(const JVal& careersJson);  // content/careers.json
  bool empty() const { return defs_.empty(); }
  const std::vector<CareerDef>& all() const { return defs_; }
  const CareerDef* def(const std::string& id) const;

  // Eligibility for every path, with met/missing reasons (reuses the unlock
  // evaluator so careers explain themselves exactly like unlocks).
  std::vector<UnlockStatus> eligibility(const PlayerCapability& cap,
                                        const UnlockCatalog* cat = nullptr,
                                        const Curriculum* cur = nullptr) const;

  // Choose a path. Refuses when entry requirements are unmet (writes why into
  // *reason). On success: sets career.path and appends to career.history
  // (deduplicating consecutive picks). Never touches evidence/missions/records.
  bool choose(Player& player, const CareerDef& def, const PlayerCapability& cap,
              const UnlockCatalog* cat = nullptr, const Curriculum* cur = nullptr,
              std::string* reason = nullptr) const;

  // Unique ids, non-empty title/language.
  std::vector<std::string> validate() const;

 private:
  std::vector<CareerDef> defs_;
  std::map<std::string, size_t> byId_;
};

}  // namespace dw
