#include "regions.h"

#include <algorithm>

namespace dw {

void Regions::load(const JVal& worldJson) {
  regions_.clear();
  for (const auto& c : worldJson.at("cities").arr) {
    RegionProfile r;
    r.cityId = c.s("id");
    if (r.cityId.empty()) continue;
    const JVal& reg = c.at("economy");  // "region" stays the string id for instancing
    r.community = reg.s("community", c.s("name"));
    r.jobFocus = reg.s("jobFocus", "General contracts");
    r.livingCost = reg.i("livingCost", 0);
    r.marketBias = reg.i("marketBias", 0);
    r.minLevel = c.i("minLevel", 1);
    regions_.push_back(r);
  }
}

const RegionProfile* Regions::profile(const std::string& cityId) const {
  for (const auto& r : regions_)
    if (r.cityId == cityId) return &r;
  return nullptr;
}

int64_t Regions::priceWithBias(int64_t base, const std::string& cityId) const {
  const RegionProfile* r = profile(cityId);
  if (!r) return base;
  int64_t v = base * (100 + r->marketBias) / 100;
  return std::max<int64_t>(1, v);
}

int64_t Regions::dailyUpkeep(const std::string& cityId) const {
  const RegionProfile* r = profile(cityId);
  return r ? r->livingCost : 0;
}

}  // namespace dw
