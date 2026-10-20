// test_achievements.cpp — the data-driven achievement system.
#include "doctest.h"
#include "achievements.h"

using namespace dw;

TEST_CASE("achievements award once and never duplicate") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"first_proof","title":"First Proof","requires":{"gates":["m13"]}}]})"));
  Player p;
  PlayerCapability cap;
  cap.completedGates = {"m13"};

  std::string firstNew;
  auto added = awardAchievements(p, c, cap, nullptr, nullptr, &firstNew);
  CHECK(added.size() == 1);
  CHECK(firstNew == "first_proof");
  CHECK(p.achievements.size() == 1);

  // Awarding again is a no-op.
  auto again = awardAchievements(p, c, cap);
  CHECK(again.empty());
  CHECK(p.achievements.size() == 1);
  CHECK(p.achievements[0] == "first_proof");
}

TEST_CASE("an unmet achievement is not awarded and explains why") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"reliable","title":"Reliable","requires":{"minReliability":70}}]})"));
  Player p;
  PlayerCapability cap;  // reliability 0
  CHECK(awardAchievements(p, c, cap).empty());
  CHECK(p.achievements.empty());
  auto why = c.progress(*c.def("reliable"), cap);
  CHECK(why.size() == 1);
}

TEST_CASE("validate rejects duplicates, missing titles and free achievements") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"a","title":"A","requires":{"level":2}},
    {"id":"a","title":"Dup","requires":{"level":3}},
    {"id":"b","title":"","requires":{"level":3}},
    {"id":"c","title":"Free","requires":{}}]})"));
  auto errs = c.validate();
  CHECK(errs.size() >= 3);
}

TEST_CASE("earned returns catalog order for a satisfied capability") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"level_10","title":"Builder","requires":{"level":10}},
    {"id":"independent","title":"On Your Own","requires":{"minIndependent":1}}]})"));
  PlayerCapability cap;
  cap.level = 10;
  cap.independentCount = 2;
  auto e = c.earned(cap);
  REQUIRE(e.size() == 2);
  CHECK(e[0] == "level_10");  // catalog order preserved
  CHECK(e[1] == "independent");
}
