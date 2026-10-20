#pragma once
// capability.h — the derived "what can this player actually do?" snapshot.
//
// The unlock catalog (unlocks.h) is evaluated against a PlayerCapability, never
// against the player's level. That is the mechanical guarantee behind objective
// 5.1/5.3: "a player's general level must not automatically grant expertise in
// every technical field" and "a player should not need to master an unrelated
// technology to access a career that matches their strengths".
//
// Every field is DERIVED from the player's raw records (level/XP, evidence,
// completed missions, reliability counters, career) — nothing is stored twice.
//
// Pure: no SDL, no I/O, no clock — callers pass `nowMs`. Design: docs/design/09.
//
// The separate records required by objective 5.1 map as follows:
//   overall level            -> PlayerCapability::level / xp
//   individual skill mastery -> exercisedSkills / independentSkills / masteredSkills
//   language proficiency     -> languages[]
//   career specialization    -> career (path + history)
//   project experience       -> completedMissions / completedGates
//   reputation/reliability   -> reputation / reliability / reliabilityTier
//   achievements/milestones  -> achievements (milestones live on CareerRecord)
//   company leadership       -> companyRank

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "curriculum.h"
#include "mastery.h"
#include "types.h"

namespace dw {

struct LanguageProficiency {
  std::string language;  // "python" | "sql" | "c" | ...
  int tier = 0;          // 0..5
  std::string label;     // "none" | "novice" | "apprentice" | "competent" | "proficient" | "expert"
  int missions = 0;      // completed missions authored in this language
  int tests = 0;         // tests those missions were judged on (breadth of work)
  int independent = 0;   // independent assessments attributed to this language
};

struct CareerInfo {
  std::string path, title, blurb;
  std::vector<std::string> history;  // every path held, oldest first
  std::vector<std::string> milestones;  // milestone ids earned, sorted
};

struct PlayerCapability {
  int level = 1;
  int64_t xp = 0;
  std::vector<std::string> exercisedSkills;    // topics with any evidence
  std::vector<std::string> independentSkills;  // independent evidence >= threshold
  std::vector<std::string> masteredSkills;     // independent and score >= mastery
  std::vector<std::string> completedMissions;  // ids, sorted
  std::vector<std::string> completedGates;     // gate ids, sorted
  int independentCount = 0;                    // independent assessments passed
  std::vector<LanguageProficiency> languages;  // sorted by language id
  CareerInfo career;
  int64_t reputation = 0;
  int reliability = 0;        // 0..100, derived from ReliabilityRecord
  std::string reliabilityTier;  // "unknown" | "unproven" | "developing" | "reliable" | "trusted" | "flawless"
  std::vector<std::string> achievements;
  int companyRank = 0;        // company reputation, 0 = none

  bool has(const std::vector<std::string>& v, const std::string& id) const {
    for (const auto& x : v)
      if (x == id) return true;
    return false;
  }
};

// Builds the snapshot. `curriculum` maps skill tags -> topics; `missions` is the
// shipped mission set (for language, gate and level metadata).
PlayerCapability deriveCapability(const Player& p,
                                  const std::map<std::string, MissionDef>& missions,
                                  const Curriculum& curriculum, const MasteryConfig& cfg,
                                  int64_t nowMs);

// Proficiency in one language, from completed missions written in it. Evidence
// is used only to count independent passes attributable to that language.
LanguageProficiency languageProficiency(const std::string& language,
                                        const std::map<std::string, MissionDef>& missions,
                                        const std::map<std::string, MissionProgress>& progress,
                                        const std::vector<Evidence>& evidence,
                                        const Curriculum& curriculum);

// 0..100 from raw outcome counters. Zero when nothing has been attempted; rises
// with completed missions and successful recalls, falls with failed attempts.
int reliabilityScore(const ReliabilityRecord& r);
std::string reliabilityTier(int score);

// Human label for a proficiency tier 0..5.
std::string proficiencyLabel(int tier);

}  // namespace dw
