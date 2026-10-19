#include "portfolio.h"

#include <algorithm>

namespace dw {

std::vector<PortfolioEntry> Portfolio::build(
    const std::map<std::string, MissionDef>& missions,
    const std::map<std::string, MissionProgress>& progress) {
  std::vector<PortfolioEntry> out;
  for (const auto& kv : missions) {
    const MissionDef& m = kv.second;
    auto pi = progress.find(m.id);
    if (pi == progress.end()) continue;
    MissionState st = pi->second.state;
    if (st == MissionState::Locked) continue;

    PortfolioEntry e;
    e.id = m.id;
    e.title = m.title;
    e.language = m.language;
    e.giver = m.giver;
    e.level = m.level;
    e.tests = (int)m.tests.size();
    e.xp = m.xpReward;
    e.gate = m.gate;
    e.completed = (st == MissionState::Complete);
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const PortfolioEntry& a, const PortfolioEntry& b) {
    if (a.completed != b.completed) return a.completed;  // finished work first
    if (a.level != b.level) return a.level < b.level;
    return a.id < b.id;
  });
  return out;
}

int Portfolio::evidenceScore(const std::vector<PortfolioEntry>& entries) {
  int score = 0;
  for (const auto& e : entries) {
    if (!e.completed) continue;
    score += e.gate ? 20 : 10;
    score += e.tests * 2;  // hidden tests are the strongest evidence
  }
  return score;
}

std::string Portfolio::evidenceTier(int score) {
  if (score >= 240) return "Exceptional";
  if (score >= 140) return "Strong";
  if (score >= 60) return "Solid";
  if (score > 0) return "Emerging";
  return "Empty";
}

int Portfolio::completedCount(const std::vector<PortfolioEntry>& entries) {
  int n = 0;
  for (const auto& e : entries)
    if (e.completed) n++;
  return n;
}

}  // namespace dw
