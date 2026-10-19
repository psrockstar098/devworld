#pragma once
// world_model.h — pure world graph: cities, map ownership, travel rules.
//
// No SDL, no rendering, no I/O. Content (content/world.json) is parsed into
// this model; the game shell asks it what the bus stop should offer and the
// tests exercise the same rules. Phase 1 gates travel on level + a proven
// mission; the design's end state gates on competency (04-progression).

#include <map>
#include <string>
#include <vector>
#include "types.h"
#include "json.h"

namespace dw {

struct CityDef {
  std::string id, name, region, blurb, spawnMap;
  int minLevel = 1;
  std::string requiresMission;        // "" = no mission requirement
  std::vector<std::string> maps;      // every map that belongs to this city
};

struct TravelLink {
  std::string from, to;               // city ids, bidirectional bus line
  int fare = 0;                       // credits charged once per trip
};

struct TravelOffer {
  std::string toCity, toName, toMap;
  int fare = 0;
  bool unlocked = false;
  std::string reason;                 // human text when locked ("Level 16 required")
};

class WorldGraph {
 public:
  void load(const JVal& j);

  const std::vector<CityDef>& cities() const { return cities_; }
  const CityDef* city(const std::string& id) const;
  const CityDef* cityForMap(const std::string& mapId) const;
  std::string cityIdForMap(const std::string& mapId) const;

  // Offers from `fromCity`, including reverse links. One offer per destination.
  // Unlocked when destination level + mission requirements are met.
  std::vector<TravelOffer> offers(const std::string& fromCity, int level,
                                  const std::map<std::string, MissionProgress>& progress) const;

  bool destinationUnlocked(const CityDef& city, int level,
                           const std::map<std::string, MissionProgress>& progress) const;

 private:
  std::vector<CityDef> cities_;
  std::vector<TravelLink> links_;
  std::map<std::string, size_t> byId_;
  std::map<std::string, std::string> mapCity_;
};

}  // namespace dw
