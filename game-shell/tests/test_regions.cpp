// tests for src/regions.h — regional living costs, market bias and job focus.
#include "doctest.h"
#include "json.h"
#include "regions.h"

using namespace dw;

static Regions sample() {
  Regions r;
  r.load(parseJson(R"({
    "cities": [
      {"id":"starter_town","name":"Starter Town","minLevel":1,
       "economy":{"community":"Weekend Hackers","jobFocus":"Fundamentals",
                 "livingCost":0,"marketBias":0}},
      {"id":"startup_city","name":"Startup City","minLevel":6,
       "economy":{"community":"Ship-It Club","jobFocus":"Product and contracts",
                 "livingCost":25,"marketBias":20}},
      {"id":"systems_district","name":"Systems District","minLevel":16,
       "economy":{"community":"Latency Guild","jobFocus":"Infrastructure",
                 "livingCost":60,"marketBias":-15}}
    ]
  })"));
  return r;
}

TEST_CASE("regions expose identity, market bias and upkeep") {
  Regions r = sample();
  REQUIRE(r.all().size() == 3);

  const RegionProfile* t = r.profile("starter_town");
  REQUIRE(t != nullptr);
  CHECK(t->community == "Weekend Hackers");
  CHECK(t->jobFocus == "Fundamentals");
  CHECK(t->minLevel == 1);

  CHECK(r.dailyUpkeep("starter_town") == 0);
  CHECK(r.dailyUpkeep("startup_city") == 25);
  CHECK(r.dailyUpkeep("systems_district") == 60);
}

TEST_CASE("market bias moves shop prices, dear and cheap") {
  Regions r = sample();
  CHECK(r.priceWithBias(100, "starter_town") == 100);     // neutral
  CHECK(r.priceWithBias(100, "startup_city") == 120);     // +20%
  CHECK(r.priceWithBias(100, "systems_district") == 85);  // -15%
  CHECK(r.priceWithBias(199, "startup_city") == 238);     // integer math
}

TEST_CASE("prices never round below one credit and unknown cities are neutral") {
  Regions r = sample();
  CHECK(r.priceWithBias(1, "systems_district") == 1);       // 0.85 -> clamped up
  CHECK(r.priceWithBias(100, "atlantis") == 100);           // unknown = base
  CHECK(r.dailyUpkeep("atlantis") == 0);
  CHECK(r.profile("atlantis") == nullptr);
}

TEST_CASE("regions default missing fields instead of crashing") {
  Regions r;
  r.load(parseJson(R"({"cities":[{"id":"nowhere","name":"Nowhere"}]})"));
  REQUIRE(r.profile("nowhere") != nullptr);
  CHECK(r.profile("nowhere")->community == "Nowhere");  // falls back to city name
  CHECK(r.profile("nowhere")->livingCost == 0);
  CHECK(r.priceWithBias(50, "nowhere") == 50);
}
