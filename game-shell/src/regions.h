#pragma once
// regions.h — pure regional identity: living costs, tech markets, job focus
// and specialist communities (design 2.3: "different regions have different
// living costs, technology markets, job opportunities and specialist
// communities"). No SDL, no I/O — content/world.json feeds this and the tests
// exercise the same numbers the city directory and the shop display.

#include <string>
#include <vector>
#include "json.h"

namespace dw {

struct RegionProfile {
  std::string cityId;
  std::string community;     // specialist community, e.g. "Weekend Hackers"
  std::string jobFocus;      // what contracts are plentiful here
  int livingCost = 0;        // credits charged per in-game day of upkeep
  int marketBias = 0;        // percent added to shop prices (+ dear, - cheap)
  int minLevel = 1;
};

class Regions {
 public:
  void load(const JVal& worldJson);            // reads world.json "cities"
  const std::vector<RegionProfile>& all() const { return regions_; }
  const RegionProfile* profile(const std::string& cityId) const;

  // Shop price in a city: base * (100 + marketBias)/100, never below 1 credit.
  int64_t priceWithBias(int64_t base, const std::string& cityId) const;

  // Daily upkeep charged while the player is active in this city.
  int64_t dailyUpkeep(const std::string& cityId) const;

 private:
  std::vector<RegionProfile> regions_;
};

}  // namespace dw
