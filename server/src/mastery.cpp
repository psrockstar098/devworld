#include "mastery.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace dw {

namespace {

const char* kKindNames[] = {
    "automated_test", "hidden_edge_case", "code_quality",    "performance",
    "explanation",    "debugging",        "delayed_recall", "transfer",
    "project_rubric"};

int kindIndex(EvidenceKind kind) {
  int i = static_cast<int>(kind);
  if (i < 0 || i >= static_cast<int>(EvidenceKind::Count)) return -1;
  return i;
}

// Working-code evidence proves the code runs. It can never, on its own, prove
// the player owns the concept — that is what independent evidence is for.
bool isWorkingCode(EvidenceKind kind) {
  switch (kind) {
    case EvidenceKind::AutomatedTest:
    case EvidenceKind::HiddenEdgeCase:
    case EvidenceKind::CodeQuality:
    case EvidenceKind::Performance:
    case EvidenceKind::Debugging:
    case EvidenceKind::ProjectRubric:
      return true;
    default:
      return false;
  }
}

// Evidence a player can only produce unaided, later, somewhere new.
bool isIndependent(EvidenceKind kind) {
  return kind == EvidenceKind::Transfer || kind == EvidenceKind::DelayedRecall;
}

}  // namespace

const char* evidenceKindName(EvidenceKind kind) {
  int i = kindIndex(kind);
  return i < 0 ? "unknown" : kKindNames[i];
}

bool evidenceKindFromString(const std::string& name, EvidenceKind& out) {
  size_t b = name.find_first_not_of(" \t");
  size_t e = name.find_last_not_of(" \t");
  std::string n;
  if (b != std::string::npos) n = name.substr(b, e - b + 1);
  for (char& c : n) {
    if (c == ' ' || c == '-') c = '_';
    c = (char)std::tolower((unsigned char)c);
  }
  for (int i = 0; i < static_cast<int>(EvidenceKind::Count); i++) {
    if (n == kKindNames[i]) {
      out = static_cast<EvidenceKind>(i);
      return true;
    }
  }
  return false;
}

double evidenceWeight(const MasteryConfig& cfg, EvidenceKind kind) {
  int i = kindIndex(kind);
  return i < 0 ? 0.0 : cfg.weight[i];
}

double decayFactor(int64_t atMs, int64_t nowMs, int64_t halfLifeMs) {
  if (halfLifeMs <= 0) return 1.0;
  int64_t elapsed = atMs >= nowMs ? 0 : nowMs - atMs;  // future evidence does not decay
  double halves = (double)elapsed / (double)halfLifeMs;
  double f = std::pow(0.5, halves);
  if (f < 0) return 0.0;
  if (f > 1) return 1.0;
  return f;
}

Evidence makeEvidence(const std::string& playerId, const std::string& skill, EvidenceKind kind,
                      double score, int64_t atMs, const std::string& source,
                      const std::string& detail) {
  Evidence e;
  e.playerId = playerId;
  e.skill = skill;
  e.kind = kind;
  e.score = std::max(0.0, std::min(1.0, score));
  e.atMs = atMs;
  e.source = source;
  e.detail = detail;
  return e;
}

std::vector<Evidence> evidenceForSkill(const std::vector<Evidence>& all, const std::string& skill) {
  std::vector<Evidence> out;
  for (const auto& e : all)
    if (e.skill == skill) out.push_back(e);
  std::sort(out.begin(), out.end(), [](const Evidence& a, const Evidence& b) {
    if (a.atMs != b.atMs) return a.atMs < b.atMs;
    return a.kind < b.kind;
  });
  return out;
}

std::string evidenceKey(const Evidence& e) {
  // One observation must never score twice: identity is (skill, kind, source,
  // time). Score is deliberately excluded so a re-grade of the same submission
  // does not shadow the original. playerId is excluded so a key stays stable
  // across a save/load round trip in the single-player shell.
  return e.skill + "|" + evidenceKindName(e.kind) + "|" + e.source + "|" +
         std::to_string(e.atMs);
}

int64_t lastEvidenceAt(const std::vector<Evidence>& all, const std::string& skill) {
  int64_t newest = 0;
  for (const auto& e : all)
    if (e.skill == skill && e.atMs > newest) newest = e.atMs;
  return newest;
}

SkillMastery summarizeSkill(const std::string& skill, const std::vector<Evidence>& all,
                            const MasteryConfig& cfg, int64_t nowMs) {
  SkillMastery sm;
  sm.skill = skill;

  std::vector<Evidence> ev = evidenceForSkill(all, skill);
  if (ev.empty()) return sm;

  double num = 0.0, den = 0.0;
  double best[static_cast<int>(EvidenceKind::Count)] = {0};
  bool seen[static_cast<int>(EvidenceKind::Count)] = {false};
  bool hasIndependent = false, hasWorking = false;
  int64_t newest = 0;

  for (const auto& e : ev) {
    int i = kindIndex(e.kind);
    if (i < 0) continue;
    double w = cfg.weight[i];
    double d = decayFactor(e.atMs, nowMs, cfg.retentionHalfLifeMs);
    num += w * d * e.score;
    den += w * d;

    if (e.score > best[i]) best[i] = e.score;
    seen[i] = true;

    if (isIndependent(e.kind) && e.score >= cfg.independentThreshold) hasIndependent = true;
    if (isWorkingCode(e.kind) && e.score >= cfg.independentThreshold) hasWorking = true;

    if (e.atMs > newest) newest = e.atMs;
  }

  sm.evidenceCount = (int)ev.size();
  sm.score = den > 0 ? num / den : 0;
  sm.lastEvidenceMs = newest;
  sm.retention = decayFactor(newest, nowMs, cfg.retentionHalfLifeMs);
  sm.independent = hasIndependent && hasWorking;
  sm.mastered = sm.independent && sm.score >= cfg.masteryThreshold;

  // Weakest kind = the evidence kind with the lowest best score (ties broken by
  // kind order, which is roughly least→most independent). Defaults to the first
  // kind a player can produce so "what should I practise?" has a sane answer.
  int weakest = -1;
  for (int i = 0; i < static_cast<int>(EvidenceKind::Count); i++) {
    if (!seen[i]) continue;
    if (weakest < 0 || best[i] < best[weakest]) weakest = i;
  }
  if (weakest < 0) {
    sm.weakest = EvidenceKind::AutomatedTest;
    sm.weakestScore = 0;
  } else {
    sm.weakest = static_cast<EvidenceKind>(weakest);
    sm.weakestScore = best[weakest];
  }

  // Spaced review: interval grows with each consecutive successful recall.
  int streak = 0;
  for (auto it = ev.rbegin(); it != ev.rend(); ++it) {
    if (it->kind != EvidenceKind::DelayedRecall) continue;
    if (it->score >= cfg.independentThreshold) streak++;
    else break;
  }
  sm.streak = streak;
  const int maxIdx = std::min(5, std::max(0, cfg.reviewIntervalCount)) - 1;
  int idx = std::min(streak, std::max(0, maxIdx));
  int64_t days = cfg.reviewIntervalCount > 0 ? cfg.reviewIntervalDays[idx] : 1;
  sm.dueAtMs = newest + days * 24LL * 3600 * 1000;

  return sm;
}

std::vector<SkillMastery> summarize(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                    int64_t nowMs) {
  std::vector<std::string> skills;
  for (const auto& e : all)
    if (!e.skill.empty()) skills.push_back(e.skill);
  std::sort(skills.begin(), skills.end());
  skills.erase(std::unique(skills.begin(), skills.end()), skills.end());

  std::vector<SkillMastery> out;
  out.reserve(skills.size());
  for (const auto& s : skills) out.push_back(summarizeSkill(s, all, cfg, nowMs));
  return out;
}

std::vector<SkillMastery> dueForReview(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                       int64_t nowMs) {
  std::vector<SkillMastery> out;
  for (auto& sm : summarize(all, cfg, nowMs))
    if (sm.evidenceCount > 0 && sm.dueAtMs <= nowMs) out.push_back(sm);
  std::sort(out.begin(), out.end(), [](const SkillMastery& a, const SkillMastery& b) {
    if (a.dueAtMs != b.dueAtMs) return a.dueAtMs < b.dueAtMs;
    return a.skill < b.skill;
  });
  return out;
}

std::vector<SkillMastery> weakestSkills(const std::vector<Evidence>& all, const MasteryConfig& cfg,
                                        int64_t nowMs, size_t limit) {
  std::vector<SkillMastery> out;
  for (auto& sm : summarize(all, cfg, nowMs))
    if (!sm.mastered) out.push_back(sm);
  std::sort(out.begin(), out.end(), [](const SkillMastery& a, const SkillMastery& b) {
    if (a.score != b.score) return a.score < b.score;
    return a.skill < b.skill;
  });
  if (out.size() > limit) out.resize(limit);
  return out;
}

bool parseEvidence(const JVal& j, Evidence& out) {
  std::string skill = j.s("skill");
  if (skill.empty()) return false;
  EvidenceKind kind = EvidenceKind::AutomatedTest;
  if (!evidenceKindFromString(j.s("kind"), kind)) return false;

  out = Evidence{};
  out.playerId = j.s("playerId");
  out.skill = skill;
  out.kind = kind;
  out.score = std::max(0.0, std::min(1.0, j.n("score", 0)));
  out.atMs = (int64_t)j.n("atMs", 0);
  out.source = j.s("source");
  out.detail = j.s("detail");
  return true;
}

}  // namespace dw
