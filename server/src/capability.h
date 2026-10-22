#pragma once
// capability.h — the derived "what can this player actually do?" snapshot.
//
// Server-authoritative copy (Phase 2): the unlock catalog (unlocks.h) is
// evaluated against a PlayerCapability, never against the player's level.
// That is the mechanical guarantee behind "a player's general level must not
// automatically grant expertise in every technical field" and "a player
// should not need to master an unrelated technology to access a career that
// matches their strengths".
//
// Every field is DERIVED from the player's raw records (level/XP, evidence,
// completed missions, reliability counters, career) — nothing is stored twice.
//
// This is the record shape only. Deriving it from progression records stays
// in the shell (deriveCapability needs the shell's Player/Mission types,
// which the server does not share); the server fills a PlayerCapability from
// its own stores when it wires the API. Pure: no SDL, no I/O, no clock.
//
// Keep in sync with game-shell/src/capability.h when the record changes.

#include <cstdint>
#include <string>
#include <vector>

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
  std::vector<std::string> history;     // every path held, oldest first
  std::vector<std::string> milestones;  // milestone ids earned, sorted
};

// Raw outcome counters. Reliability is a record of behaviour, not a default.
struct ReliabilityRecord {
  int passes = 0;   // missions completed
  int fails = 0;    // failed test submissions
  int recalls = 0;  // delayed-recall practice passes
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
  std::string reliabilityTier;  // "unproven" | "developing" | "reliable" | "trusted" | "flawless"
  std::vector<std::string> achievements;
  int companyRank = 0;        // company reputation, 0 = none

  bool has(const std::vector<std::string>& v, const std::string& id) const {
    for (const auto& x : v)
      if (x == id) return true;
    return false;
  }
};

// 0..100 from raw outcome counters. Zero when nothing has been attempted; rises
// with completed missions and successful recalls, falls with failed attempts.
int reliabilityScore(const ReliabilityRecord& r);
std::string reliabilityTier(int score);

// Human label for a proficiency tier 0..5.
std::string proficiencyLabel(int tier);

}  // namespace dw
