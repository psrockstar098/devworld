// capability.cpp — derive the player-capability snapshot (see capability.h).
#include "capability.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dw {

namespace {

bool isIndependentKind(EvidenceKind k) {
  return k == EvidenceKind::Transfer || k == EvidenceKind::DelayedRecall;
}

}  // namespace

std::string proficiencyLabel(int tier) {
  switch (tier) {
    case 0: return "none";
    case 1: return "novice";
    case 2: return "apprentice";
    case 3: return "competent";
    case 4: return "proficient";
    default: return "expert";
  }
}

int reliabilityScore(const ReliabilityRecord& r) {
  // Nothing attempted scores nothing: reliability is a record of behaviour, not
  // a default. The ratio measures follow-through; recalls add consistency.
  int passes = std::max(0, r.passes);
  int fails = std::max(0, r.fails);
  int denom = passes + fails;
  int ratio = denom > 0 ? (int)std::lround(100.0 * passes / denom) : 0;
  int score = ratio + 2 * std::max(0, r.recalls);
  return std::max(0, std::min(100, score));
}

std::string reliabilityTier(int score) {
  if (score <= 0) return "unproven";
  if (score < 40) return "developing";
  if (score < 70) return "reliable";
  if (score < 90) return "trusted";
  return "flawless";
}

LanguageProficiency languageProficiency(const std::string& language,
                                        const std::map<std::string, MissionDef>& missions,
                                        const std::map<std::string, MissionProgress>& progress,
                                        const std::vector<Evidence>& evidence,
                                        const Curriculum& curriculum) {
  LanguageProficiency lp;
  lp.language = language;
  for (const auto& kv : missions) {
    const MissionDef& m = kv.second;
    if (m.language != language) continue;
    auto pi = progress.find(m.id);
    if (pi == progress.end() || pi->second.state != MissionState::Complete) continue;
    lp.missions++;
    lp.tests += (int)m.tests.size();

    // Independent work in this language: any topic the mission teaches that has
    // independent evidence attributable to the player.
    bool independent = false;
    for (const auto& topic : curriculum.topicsForMission(m.id)) {
      for (const auto& e : evidence) {
        if (e.skill == topic && isIndependentKind(e.kind)) {
          independent = true;
          break;
        }
      }
      if (independent) break;
    }
    if (independent) lp.independent++;
  }
  // Breadth of completed work sets the tier; one solid mission opens the path.
  lp.tier = lp.missions == 0 ? 0 : std::min(5, 1 + lp.tests / 4);
  lp.label = proficiencyLabel(lp.tier);
  return lp;
}

PlayerCapability deriveCapability(const Player& p,
                                  const std::map<std::string, MissionDef>& missions,
                                  const Curriculum& curriculum, const MasteryConfig& cfg,
                                  int64_t nowMs) {
  PlayerCapability cap;
  cap.level = p.levels.level;
  cap.xp = p.levels.xp;
  cap.reputation = p.reputation;
  cap.achievements = p.achievements;
  cap.companyRank = p.company.reputation;

  // Skill mastery comes only from evidence — never from the level.
  for (const auto& sm : summarize(p.evidence, cfg, nowMs)) {
    cap.exercisedSkills.push_back(sm.skill);
    if (sm.independent) cap.independentSkills.push_back(sm.skill);
    if (sm.mastered) cap.masteredSkills.push_back(sm.skill);
  }
  std::sort(cap.exercisedSkills.begin(), cap.exercisedSkills.end());
  std::sort(cap.independentSkills.begin(), cap.independentSkills.end());
  std::sort(cap.masteredSkills.begin(), cap.masteredSkills.end());

  for (const auto& e : p.evidence)
    if (isIndependentKind(e.kind) && e.score >= cfg.independentThreshold) cap.independentCount++;

  for (const auto& kv : p.missions) {
    if (kv.second.state != MissionState::Complete) continue;
    cap.completedMissions.push_back(kv.first);
    auto mi = missions.find(kv.first);
    if (mi != missions.end() && mi->second.gate) cap.completedGates.push_back(kv.first);
  }
  std::sort(cap.completedMissions.begin(), cap.completedMissions.end());
  std::sort(cap.completedGates.begin(), cap.completedGates.end());

  // One proficiency record per language that has shipped missions.
  std::set<std::string> langs;
  for (const auto& kv : missions) langs.insert(kv.second.language);
  for (const auto& lang : langs)
    cap.languages.push_back(
        languageProficiency(lang, missions, p.missions, p.evidence, curriculum));

  cap.career.path = p.career.path;
  cap.career.history = p.career.history;
  for (const auto& kv : p.career.milestones) cap.career.milestones.push_back(kv.first);
  std::sort(cap.career.milestones.begin(), cap.career.milestones.end());

  cap.reliability = reliabilityScore(p.reliability);
  cap.reliabilityTier = reliabilityTier(cap.reliability);
  return cap;
}

}  // namespace dw
