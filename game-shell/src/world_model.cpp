#include "world_model.h"
#include "json.h"

#include <algorithm>

namespace dw {

void WorldGraph::load(const JVal& j) {
  cities_.clear();
  links_.clear();
  byId_.clear();
  mapCity_.clear();

  for (const auto& c : j.at("cities").arr) {
    CityDef d;
    d.id = c.s("id");
    d.name = c.s("name");
    d.region = c.s("region", d.id);
    d.blurb = c.s("blurb");
    d.spawnMap = c.s("spawnMap");
    d.minLevel = c.i("minLevel", 1);
    d.requiresMission = c.s("requiresMission");
    for (const auto& m : c.at("maps").arr) d.maps.push_back(m.str);
    if (d.id.empty()) continue;
    byId_[d.id] = cities_.size();
    for (const auto& m : d.maps) mapCity_[m] = d.id;
    cities_.push_back(d);
  }

  for (const auto& l : j.at("links").arr) {
    TravelLink t;
    t.from = l.s("from");
    t.to = l.s("to");
    t.fare = l.i("fare", 0);
    if (byId_.count(t.from) && byId_.count(t.to) && t.from != t.to)
      links_.push_back(t);
  }
}

const CityDef* WorldGraph::city(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &cities_[it->second];
}

const CityDef* WorldGraph::cityForMap(const std::string& mapId) const {
  auto it = mapCity_.find(mapId);
  return it == mapCity_.end() ? nullptr : city(it->second);
}

std::string WorldGraph::cityIdForMap(const std::string& mapId) const {
  auto it = mapCity_.find(mapId);
  return it == mapCity_.end() ? "" : it->second;
}

bool WorldGraph::destinationUnlocked(
    const CityDef& c, int level,
    const std::map<std::string, MissionProgress>& progress) const {
  if (level < c.minLevel) return false;
  if (!c.requiresMission.empty()) {
    auto it = progress.find(c.requiresMission);
    if (it == progress.end() || it->second.state != MissionState::Complete) return false;
  }
  return true;
}

std::vector<TravelOffer> WorldGraph::offers(
    const std::string& fromCity, int level,
    const std::map<std::string, MissionProgress>& progress) const {
  std::vector<TravelOffer> out;
  std::map<std::string, size_t> seen;

  auto consider = [&](const std::string& other, int fare) {
    if (other == fromCity) return;
    auto it = byId_.find(other);
    if (it == byId_.end()) return;
    auto dup = seen.find(other);
    if (dup != seen.end()) {
      out[dup->second].fare = std::min(out[dup->second].fare, fare == 0 ? out[dup->second].fare : fare);
      return;
    }
    const CityDef& c = cities_[it->second];
    TravelOffer o;
    o.toCity = c.id;
    o.toName = c.name;
    o.toMap = c.spawnMap;
    o.fare = fare;
    o.unlocked = destinationUnlocked(c, level, progress);
    if (!o.unlocked) {
      if (level < c.minLevel)
        o.reason = "Level " + std::to_string(c.minLevel) + " required";
      else if (!c.requiresMission.empty())
        o.reason = "Proof of Skill required: " + c.requiresMission;
      else
        o.reason = "Locked";
    }
    seen[other] = out.size();
    out.push_back(o);
  };

  for (const auto& l : links_) {
    if (l.from == fromCity) consider(l.to, l.fare);
    else if (l.to == fromCity) consider(l.from, l.fare);
  }

  std::sort(out.begin(), out.end(), [](const TravelOffer& a, const TravelOffer& b) {
    if (a.unlocked != b.unlocked) return a.unlocked > b.unlocked;  // open routes first
    return a.toName < b.toName;
  });
  return out;
}

}  // namespace dw
