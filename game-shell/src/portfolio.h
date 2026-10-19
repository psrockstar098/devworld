#pragma once
// portfolio.h — project portfolio and reputation (design 2.3).
//
// The portfolio is DERIVED, not stored: it is a view over mission progress. Each
// shipped project lists its language, level, and the number of test cases it had
// to satisfy — the evidence that the work is real. Reputation tiers come from
// the bounded evidence score, so nothing here can be inflated by cosmetics.
//
// Pure: no SDL, no I/O.

#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct PortfolioEntry {
  std::string id, title, language, giver;
  int level = 1;
  int tests = 0;        // test cases the mission is judged on
  int64_t xp = 0;
  bool gate = false;
  bool completed = false;
};

class Portfolio {
 public:
  // Every mission that is at least available, completed first, then by level.
  static std::vector<PortfolioEntry> build(
      const std::map<std::string, MissionDef>& missions,
      const std::map<std::string, MissionProgress>& progress);

  // Evidence score: gate completions and hidden tests count for more.
  static int evidenceScore(const std::vector<PortfolioEntry>& entries);

  static std::string evidenceTier(int score);

  static int completedCount(const std::vector<PortfolioEntry>& entries);
};

}  // namespace dw
