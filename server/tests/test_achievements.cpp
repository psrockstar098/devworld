// test_achievements.cpp — the data-driven achievement system (Stage 5).
// Mirrors the shell's test_achievements.cpp, adapted: the server has no
// shell Player, so awarding takes the held-achievement list directly.
// Plus the real shipped catalog.
#include "doctest.h"
#include "achievements.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace dw;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) return "";
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("achievements award once and never duplicate") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"first_proof","title":"First Proof","requires":{"gates":["m13"]}}]})"));
  std::vector<std::string> held;
  PlayerCapability cap;
  cap.completedGates = {"m13"};

  std::string firstNew;
  auto added = awardAchievements(held, c, cap, nullptr, nullptr, &firstNew);
  CHECK(added.size() == 1);
  CHECK(firstNew == "first_proof");
  CHECK(held.size() == 1);

  // Awarding again is a no-op.
  auto again = awardAchievements(held, c, cap);
  CHECK(again.empty());
  CHECK(held.size() == 1);
  CHECK(held[0] == "first_proof");
}

TEST_CASE("an unmet achievement is not awarded and explains why") {
  AchievementCatalog c;
  c.load(parseJson(R"({"achievements":[
    {"id":"reliable","title":"Reliable","requires":{"minReliability":70}}]})"));
  std::vector<std::string> held;
  PlayerCapability cap;  // reliability 0
  CHECK(awardAchievements(held, c, cap).empty());
  CHECK(held.empty());
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

TEST_CASE("the shipped achievement catalog loads and validates") {
  std::string text = readFile("../game-shell/content/achievements.json");
  if (text.empty()) {
    MESSAGE("skipping shipped achievements: file not found");
    return;
  }
  AchievementCatalog c;
  c.load(parseJson(text));
  CHECK_FALSE(c.empty());
  CHECK(c.all().size() == 15);
  auto errs = c.validate();
  for (const auto& e : errs) INFO(e);
  CHECK(errs.empty());
  // First Steps is earned by completing m01 and nothing else.
  PlayerCapability cap;
  cap.completedMissions = {"m01"};
  auto earned = c.earned(cap);
  CHECK(std::find(earned.begin(), earned.end(), "first_steps") != earned.end());
}
