// capability.h — pure capability helpers (see capability.h).
#include "capability.h"

#include <algorithm>
#include <cmath>

namespace dw {

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

}  // namespace dw
