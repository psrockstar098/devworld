#pragma once
// mastery.h — evidence-based mastery (design 08 §4, Stage 4 doc 11).
//
// Server-authoritative copy (Phase 2): the server computes mastery from
// evidence; the client never computes it for anything valuable. The shell
// keeps its own copy for offline play. Keep the two in sync when the model
// changes.
//
// XP, speed, playtime and "finished the tutorial" are deliberately NOT inputs.
// The only input is evidence. Deterministic tests prove that code works; they
// do not prove the player owns the concept. So:
//
//   * `score` is a weighted, time-decayed mean of all evidence for a skill.
//   * `independent` requires Transfer or DelayedRecall evidence above
//     `independentThreshold` AND working-code evidence (tests, hidden tests,
//     debugging, performance, quality or rubric) above it.
//   * `mastered` requires `independent` and `score >= masteryThreshold`.
//
// A player who only passes the shipped tests therefore scores well but is
// never "independent" and never "mastered". That is the distinction the
// education spec asks for, enforced in code rather than in prose.
//
// Pure: no SDL, no I/O, no clock — callers pass `nowMs`, so results are
// deterministic and reproducible. Design: docs/design/08-education-system.md
// and docs/design/11-learning-engine-mastery.md.

#include <cstdint>
#include <string>
#include <vector>

#include "json.h"

namespace dw {

enum class EvidenceKind {
  AutomatedTest = 0,  // shipped tests pass
  HiddenEdgeCase,     // hidden tests pass
  CodeQuality,        // maintainability review
  Performance,        // time/memory constraint met
  Explanation,        // explains why the behaviour happens
  Debugging,          // finds and repairs a defect
  DelayedRecall,      // revisits the skill later, unaided
  Transfer,           // solves an unseen problem in a new context/language
  ProjectRubric,      // larger project scored against a rubric
  Count
};

const char* evidenceKindName(EvidenceKind kind);
bool evidenceKindFromString(const std::string& name, EvidenceKind& out);

struct Evidence {
  std::string playerId;
  std::string skill;  // curriculum topic id
  EvidenceKind kind = EvidenceKind::AutomatedTest;
  double score = 0;   // 0..1
  int64_t atMs = 0;   // observation time, epoch milliseconds
  std::string source; // mission id, review id, project id
  std::string detail;
};

struct MasteryConfig {
  // Indexed by EvidenceKind. Deterministic tests carry the least weight;
  // transfer and delayed recall carry the most.
  double weight[static_cast<int>(EvidenceKind::Count)] = {1.0, 1.4, 1.2, 1.1, 1.6,
                                                          1.5, 1.7, 2.0, 1.6};
  double masteryThreshold = 0.80;
  double independentThreshold = 0.60;
  int64_t retentionHalfLifeMs = 60LL * 24 * 3600 * 1000;  // 60 days
  // A revisit only counts as delayed recall once enough time has passed that
  // the player cannot be reciting from short-term memory (design 08 §4.4).
  int64_t recallMinAgeMs = 30LL * 60 * 1000;  // 30 minutes
  // Days until the next review, indexed by consecutive successful recalls.
  int64_t reviewIntervalDays[5] = {1, 3, 7, 16, 35};
  int reviewIntervalCount = 5;
};

struct SkillMastery {
  std::string skill;
  double score = 0;      // 0..1, weighted and decayed
  double retention = 0;  // 0..1, decay factor of the newest evidence
  bool independent = false;
  bool mastered = false;
  int evidenceCount = 0;
  EvidenceKind weakest = EvidenceKind::Explanation;
  double weakestScore = 0;  // best raw score seen for the weakest kind
  int64_t lastEvidenceMs = 0;
  int64_t dueAtMs = 0;  // next spaced review
  int streak = 0;       // consecutive successful delayed recalls
};

double evidenceWeight(const MasteryConfig& cfg, EvidenceKind kind);

// 0.5 ^ (elapsed / halfLife), clamped to [0,1]; future timestamps do not decay.
double decayFactor(int64_t atMs, int64_t nowMs, int64_t halfLifeMs);

SkillMastery summarizeSkill(const std::string& skill, const std::vector<Evidence>& all,
                            const MasteryConfig& cfg, int64_t nowMs);

std::vector<SkillMastery> summarize(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                    int64_t nowMs);

// Skills whose spaced review is due, soonest first.
std::vector<SkillMastery> dueForReview(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                       int64_t nowMs);

// Unmastered skills with the weakest evidence, for "what should I practise?".
std::vector<SkillMastery> weakestSkills(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                        int64_t nowMs, size_t limit);

std::vector<Evidence> evidenceForSkill(const std::vector<Evidence>& all, const std::string& skill);

// When the skill was last exercised (0 if never). Callers use this to decide
// whether a revisit is a delayed recall (design 08 §4.4).
int64_t lastEvidenceAt(const std::vector<Evidence>& all, const std::string& skill);

Evidence makeEvidence(const std::string& playerId, const std::string& skill, EvidenceKind kind,
                      double score, int64_t atMs, const std::string& source = "",
                      const std::string& detail = "");

// Parses { playerId, skill, kind, score, atMs, source, detail }.
bool parseEvidence(const JVal& j, Evidence& out);

// Stable identity for deduplication (same observation must not score twice).
std::string evidenceKey(const Evidence& e);

}  // namespace dw
