#pragma once
// company.h — company management (design 2.3). Found a studio, pick a focus,
// staff it from your contacts, and watch reputation and contract volume grow.
//
// The company is a progression-neutral convenience: rank and payout multipliers
// are flavour plus rewards that never touch test verdicts (same rule as items).
//
// Pure: no SDL, no I/O. Focus list lives in code so it is easy to test.
// [PHASE2] the company, roster and contracts become server records.

#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct CompanyInfo {
  bool founded = false;
  std::string name, focus, focusBlurb, rank;
  int reputation = 0;
  int contracts = 0;
  int members = 0;
};

class Company {
 public:
  static const std::vector<std::string>& focuses();

  // 3..24 chars, letters/digits/underscore/space, must contain a letter.
  static bool validName(const std::string& name, std::string* reason = nullptr);

  static bool validFocus(const std::string& focus);

  static std::string focusBlurb(const std::string& focus);

  static std::string rankFor(int reputation);

  // Found the company: validates name and focus, resets the record.
  static bool found(Player& player, const std::string& name, const std::string& focus,
                    std::string* reason = nullptr);

  // Reputation from completed missions and contracts (derived, never stored
  // twice): gate completions weigh more than normal ones.
  static int reputationFrom(const std::map<std::string, MissionProgress>& missions,
                            int contracts);

  static CompanyInfo info(const Player& player);

  // Contract payout: base, scaled by rank reputation and team size. Convenience
  // rewards only — never used to judge a mission/test.
  static int64_t contractPayout(int64_t base, int reputation, int teamSize);
};

}  // namespace dw
