// tests for src/dungeons.h — hidden technology dungeons: discovery and gating.
#include "doctest.h"
#include "dungeons.h"
#include "json.h"

using namespace dw;

static Dungeons sample() {
  Dungeons d;
  d.load(parseJson(R"({
    "dungeons": [
      {"id":"forgotten_repo","name":"The Forgotten Repository",
       "cityId":"starter_town","entranceMap":"town","interiorMap":"dungeon_repo",
       "entranceX":9,"entranceY":3,
       "minLevel":3,"missionId":"m06","rewardItem":"git_relic",
       "rewardText":"Version control relic"},
      {"id":"derelict_dc","name":"Derelict Data Center",
       "cityId":"startup_city","entranceMap":"startup_city","interiorMap":"dungeon_datacenter",
       "entranceX":4,"entranceY":4,
       "minLevel":10,"missionId":"m12","rewardItem":"rack_unit",
       "rewardText":"Salvaged rack unit"}
    ]
  })"));
  return d;
}

TEST_CASE("dungeons load with their secret metadata") {
  Dungeons d = sample();
  REQUIRE(d.all().size() == 2);
  const DungeonDef* f = d.byId("forgotten_repo");
  REQUIRE(f != nullptr);
  CHECK(f->cityId == "starter_town");
  CHECK(f->entranceMap == "town");
  CHECK(f->interiorMap == "dungeon_repo");
  CHECK(f->minLevel == 3);
  CHECK(f->rewardItem == "git_relic");
  CHECK(d.byId("nope") == nullptr);
}

TEST_CASE("an undiscovered dungeon is invisible and cannot be entered") {
  Dungeons d = sample();
  const DungeonDef* f = d.byId("forgotten_repo");
  std::map<std::string, std::string> flags;  // nothing found yet
  CHECK_FALSE(d.revealed(*f, flags));
  CHECK_FALSE(d.canEnter(*f, 99, flags));
  CHECK(d.atMap("town", flags).empty());
  CHECK(d.lockReason(*f, 99, flags).find("not found") != std::string::npos);
}

TEST_CASE("discovering a dungeon reveals its entrance on the right map") {
  Dungeons d = sample();
  const DungeonDef* f = d.byId("forgotten_repo");
  std::map<std::string, std::string> flags{{Dungeons::revealFlag("forgotten_repo"), "1"}};
  CHECK(d.revealed(*f, flags));
  auto here = d.atMap("town", flags);
  REQUIRE(here.size() == 1);
  CHECK(here[0]->id == "forgotten_repo");
  CHECK(d.atMap("startup_city", flags).empty());   // not on other maps
}

TEST_CASE("the level gate blocks entry even after discovery") {
  Dungeons d = sample();
  const DungeonDef* f = d.byId("forgotten_repo");
  std::map<std::string, std::string> flags{{Dungeons::revealFlag("forgotten_repo"), "1"}};
  CHECK_FALSE(d.canEnter(*f, 2, flags));
  CHECK(d.lockReason(*f, 2, flags).find("Level 3") != std::string::npos);
  CHECK(d.canEnter(*f, 3, flags));
  CHECK(d.lockReason(*f, 3, flags).empty());
}

TEST_CASE("a falsy reveal flag keeps the secret") {
  Dungeons d = sample();
  const DungeonDef* f = d.byId("forgotten_repo");
  CHECK_FALSE(d.revealed(*f, {{Dungeons::revealFlag("forgotten_repo"), "0"}}));
  CHECK_FALSE(d.revealed(*f, {{Dungeons::revealFlag("forgotten_repo"), "false"}}));
  CHECK_FALSE(d.revealed(*f, {{Dungeons::revealFlag("forgotten_repo"), ""}}));
}

TEST_CASE("flags and clear-state names are stable") {
  CHECK(Dungeons::revealFlag("x") == "dungeon_x");
  CHECK(Dungeons::doneFlag("x") == "dungeon_cleared_x");
}
