// tests for src/profile.h — reputation tiers, profile summary, notice log.
#include "doctest.h"
#include "profile.h"
#include "progression.h"

using namespace dw;

TEST_CASE("reputation tiers advance at the documented thresholds") {
  CHECK(reputationTier(0) == "Newcomer");
  CHECK(reputationTier(9) == "Newcomer");
  CHECK(reputationTier(10) == "Trusted");
  CHECK(reputationTier(49) == "Trusted");
  CHECK(reputationTier(50) == "Respected");
  CHECK(reputationTier(149) == "Respected");
  CHECK(reputationTier(150) == "Veteran");
  CHECK(reputationTier(399) == "Veteran");
  CHECK(reputationTier(400) == "Legend");
  CHECK(reputationTier(100000) == "Legend");
}

TEST_CASE("profile reflects level, title, reputation and playtime") {
  Player p;
  p.name = "Ada"; p.displayName = "Ada L.";
  p.levels.level = 6; p.levels.xp = 250;
  p.reputation = 55;
  p.playSeconds = 125;  // 2 minutes

  ProfileSummary s = profileFor(p, 4, 2);
  CHECK(s.name == "Ada");
  CHECK(s.displayName == "Ada L.");
  CHECK(s.level == 6);
  CHECK(s.title == titleForLevel(6));
  CHECK(s.xp == 250);
  CHECK(s.xpToNext == xpForLevel(6));
  CHECK(s.reputationTier == "Respected");
  CHECK(s.missionsComplete == 4);
  CHECK(s.missionsActive == 2);
  CHECK(s.playMinutes == 2);
}

TEST_CASE("visited cities are derived from achievements, sorted and unique") {
  Player p;
  p.achievements = {"visited_startup_city", "level_5", "visited_starter_town",
                    "visited_startup_city"};
  ProfileSummary s = profileFor(p, 0, 0);
  REQUIRE(s.visitedCities.size() == 2);
  CHECK(s.visitedCities[0] == "starter_town");
  CHECK(s.visitedCities[1] == "startup_city");
  CHECK(s.achievements.size() == 4);  // raw list untouched
}

TEST_CASE("loadout lists equipped slots sorted by slot name") {
  Player p;
  p.equipped["tool"] = "cheatsheets";
  p.equipped["computer"] = "laptop_pro";
  ProfileSummary s = profileFor(p, 0, 0);
  REQUIRE(s.loadout.size() == 2);
  CHECK(s.loadout[0] == "computer:laptop_pro");
  CHECK(s.loadout[1] == "tool:cheatsheets");
}

TEST_CASE("the notice log keeps the newest entries and trims the oldest") {
  std::vector<Notice> log;
  for (int i = 0; i < 8; ++i) {
    Notice n; n.type = "info"; n.text = "n" + std::to_string(i);
    pushNotice(log, n, 5);
  }
  REQUIRE(log.size() == 5);
  CHECK(log.front().text == "n3");  // n0..n2 trimmed
  CHECK(log.back().text == "n7");
}
