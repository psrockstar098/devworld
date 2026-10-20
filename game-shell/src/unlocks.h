#pragma once
// unlocks.h — the unlock catalog and the prerequisite engine (objective 5.3).
//
// Every open-world opportunity (a bus route, company eligibility, a technical
// privilege, a piece of equipment, a job tier, a career) is one data-driven
// UnlockDef. Each is evaluated against a PlayerCapability — never against the
// player's level — and returns *why* it was earned or not, so the dashboard can
// always explain an unlock (the spec's completion criterion).
//
// The requirement axes are deliberately separate: city access is breadth
// (level + missions), company eligibility is reputation/leadership, and a
// technical privilege names the one knowledge area it belongs to so a player
// "should not need to master an unrelated technology to access a career that
// matches their strengths" (objective 5.3). `validate()` fails content that
// breaks these rules.
//
// Pure: no SDL, no I/O, no clock. Design: docs/design/09-progression-engine.md.

#include <map>
#include <string>
#include <vector>

#include "capability.h"
#include "curriculum.h"
#include "json.h"

namespace dw {

enum class UnlockKind {
  CityAccess,          // a route or district opens (breadth only)
  CompanyEligibility,  // found/upgrade a company (leadership, not tech)
  TechnicalPrivilege,  // a tool/privilege tied to one knowledge area
  Equipment,           // an item or upgrade becomes buyable
  JobTier,             // a class of contract unlocks
  Career,              // a career path becomes selectable
};

const char* unlockKindName(UnlockKind k);
bool unlockKindFromString(const std::string& s, UnlockKind& out);

// One requirement axis set. Every field is optional; an unlock is earned when
// all present axes hold. Each axis maps to exactly one capability record.
struct UnlockRequirement {
  int minLevel = 0;                             // overall level (XP breadth)
  std::vector<std::string> missions;            // completed mission ids
  std::vector<std::string> gates;               // completed gate ids
  std::vector<std::string> milestones;          // earned milestone ids
  std::vector<std::string> skills;              // topics that must be mastered
  std::vector<std::string> independentSkills;   // topics that must be independent
  int minIndependent = 0;                       // independent evidence count
  int minReliability = 0;                       // reliability score (0..100)
  bool requiresCompany = false;                 // a company must be founded
  int minCompanyRep = 0;                        // company reputation floor
};

struct UnlockDef {
  std::string id, title, blurb;
  UnlockKind kind = UnlockKind::TechnicalPrivilege;
  std::string area;  // technical privileges: the only area their skills may come from
  UnlockRequirement requires;
};

struct UnlockStatus {
  std::string id;
  bool unlocked = false;
  std::vector<std::string> met;      // "why you earned it" lines
  std::vector<std::string> missing;  // "why not yet" lines
};

// A named career milestone, earned by completing `mission`. Milestones live in
// the catalog (unlocks.json "milestones") so requirement text stays data-driven.
struct MilestoneDef {
  std::string id, title, mission;
};

class UnlockCatalog {
 public:
  void load(const JVal& catalogJson);  // content/unlocks.json
  bool empty() const { return defs_.empty(); }

  const std::vector<UnlockDef>& all() const { return defs_; }
  const std::vector<MilestoneDef>& milestones() const { return milestones_; }
  const UnlockDef* def(const std::string& id) const;
  const MilestoneDef* milestone(const std::string& id) const;
  bool milestoneEarned(const std::string& id, const PlayerCapability& cap) const;

  // Evaluate one unlock. `cur` is optional and used only for nicer reasons
  // (topic titles); pass nullptr and ids are shown instead.
  UnlockStatus evaluate(const UnlockDef& d, const PlayerCapability& cap,
                        const Curriculum* cur = nullptr) const;

  // Every unlock, sorted by id, with reasons.
  std::vector<UnlockStatus> evaluateAll(const PlayerCapability& cap,
                                        const Curriculum* cur = nullptr) const;

  const UnlockDef* firstOfKind(UnlockKind kind) const;

  // Content errors; empty means the catalog is sound. Enforces the separation
  // rules: unique ids, known kinds, technical privileges name an area and a
  // competency (never level alone), privileges only reference their own area,
  // city access carries no skill requirements, milestones resolve to a mission.
  std::vector<std::string> validate() const;

 private:
  std::vector<UnlockDef> defs_;
  std::map<std::string, size_t> byId_;
  std::vector<MilestoneDef> milestones_;
  std::map<std::string, size_t> milestoneById_;
};

// Shared requirement evaluator, used by the unlock catalog and the achievement
// system. Returns human "why earned" / "why not yet" lines. `cat` resolves
// milestones (may be null); `cur` supplies topic titles (may be null).
std::vector<std::string> metRequirements(const UnlockRequirement& q, const PlayerCapability& cap,
                                         const UnlockCatalog* cat = nullptr,
                                         const Curriculum* cur = nullptr);
std::vector<std::string> unmetRequirements(const UnlockRequirement& q, const PlayerCapability& cap,
                                           const UnlockCatalog* cat = nullptr,
                                           const Curriculum* cur = nullptr);
bool requirementEmpty(const UnlockRequirement& q);

// Parse a "requires" object; shared by unlocks, achievements and careers.
UnlockRequirement parseRequirement(const JVal& requiresJson);

}  // namespace dw
