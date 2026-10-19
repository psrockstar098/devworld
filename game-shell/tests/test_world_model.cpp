// tests for src/world_model.h — cities, map ownership and travel gating.
#include "doctest.h"
#include "world_model.h"
#include "json.h"

using namespace dw;

static const char* kWorld = R"({
  "cities": [
    {"id":"starter_town","name":"Starter Town","region":"region_starter",
     "blurb":"start","spawnMap":"town","minLevel":1,"requiresMission":"",
     "maps":["town","lab_interior"]},
    {"id":"startup_city","name":"Startup City","region":"region_startup",
     "blurb":"ship","spawnMap":"startup_city_town","minLevel":6,"requiresMission":"m14",
     "maps":["startup_city_town","startup_hub"]},
    {"id":"systems_district","name":"Systems District","region":"region_systems",
     "blurb":"hum","spawnMap":"systems_district_town","minLevel":16,"requiresMission":"",
     "maps":["systems_district_town"]}
  ],
  "links": [
    {"from":"starter_town","to":"startup_city","fare":80},
    {"from":"startup_city","to":"systems_district","fare":200},
    {"from":"nope","to":"starter_town","fare":10}
  ]
})";

static WorldGraph loadGraph() {
  WorldGraph g;
  g.load(parseJson(kWorld));
  return g;
}

static std::map<std::string, MissionProgress> done(const std::string& id) {
  std::map<std::string, MissionProgress> p;
  MissionProgress mp;
  mp.state = MissionState::Complete;
  p[id] = mp;
  return p;
}

TEST_CASE("graph parses cities, owns maps and drops dangling links") {
  WorldGraph g = loadGraph();
  REQUIRE(g.cities().size() == 3);
  CHECK(g.city("starter_town")->name == "Starter Town");
  CHECK(g.city("nope") == nullptr);

  CHECK(g.cityIdForMap("startup_hub") == "startup_city");
  CHECK(g.cityForMap("systems_district_town")->region == "region_systems");
  CHECK(g.cityIdForMap("unknown") == "");
  CHECK(g.cityForMap("unknown") == nullptr);

  // The "nope" -> starter_town link is not offered from starter_town because
  // the unknown city has no entry; no crash, no ghost destination.
  auto offers = g.offers("starter_town", 99, {});
  for (const auto& o : offers) CHECK(o.toCity != "nope");
}

TEST_CASE("travel offers are bidirectional with the link fare") {
  WorldGraph g = loadGraph();
  auto fromStarter = g.offers("starter_town", 50, done("m14"));
  REQUIRE(fromStarter.size() == 1);
  CHECK(fromStarter[0].toCity == "startup_city");
  CHECK(fromStarter[0].fare == 80);
  CHECK(fromStarter[0].toMap == "startup_city_town");
  CHECK(fromStarter[0].unlocked);

  auto fromStartup = g.offers("startup_city", 50, done("m14"));
  REQUIRE(fromStartup.size() == 2);
  // Both directions present: back to starter, onward to systems.
  CHECK(fromStartup[0].toCity == "starter_town");
  CHECK(fromStartup[0].fare == 80);
  CHECK(fromStartup[1].toCity == "systems_district");
  CHECK(fromStartup[1].fare == 200);
}

TEST_CASE("level gates lock a route with a readable reason") {
  WorldGraph g = loadGraph();
  auto offers = g.offers("starter_town", 5, done("m14"));
  REQUIRE(offers.size() == 1);
  CHECK_FALSE(offers[0].unlocked);
  CHECK(offers[0].reason == "Level 6 required");
}

TEST_CASE("mission gates lock a route even at the right level") {
  WorldGraph g = loadGraph();
  auto offers = g.offers("starter_town", 20, {});  // no m14
  REQUIRE(offers.size() == 1);
  CHECK_FALSE(offers[0].unlocked);
  CHECK(offers[0].reason.find("m14") != std::string::npos);

  auto withProof = g.offers("starter_town", 20, done("m14"));
  CHECK(withProof[0].unlocked);
  CHECK(withProof[0].reason.empty());
}

TEST_CASE("unlocked routes sort before locked ones") {
  WorldGraph g = loadGraph();
  auto offers = g.offers("startup_city", 10, done("m14"));
  REQUIRE(offers.size() == 2);
  CHECK(offers[0].unlocked);
  CHECK(offers[0].toCity == "starter_town");
  CHECK(offers[1].toCity == "systems_district");
  CHECK(offers[1].reason == "Level 16 required");
}

TEST_CASE("an unknown city has no route out") {
  WorldGraph g = loadGraph();
  CHECK(g.offers("nowhere", 99, {}).empty());
}

TEST_CASE("destinationUnlocked checks level and mission independently") {
  WorldGraph g = loadGraph();
  const CityDef* startup = g.city("startup_city");
  REQUIRE(startup != nullptr);
  CHECK_FALSE(g.destinationUnlocked(*startup, 5, done("m14")));
  CHECK_FALSE(g.destinationUnlocked(*startup, 6, {}));
  CHECK(g.destinationUnlocked(*startup, 6, done("m14")));
  CHECK(g.destinationUnlocked(*startup, 50, done("m14")));

  const CityDef* starter = g.city("starter_town");
  CHECK(g.destinationUnlocked(*starter, 1, {}));
}
