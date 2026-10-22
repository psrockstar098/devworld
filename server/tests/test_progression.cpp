#include "doctest.h"

#include "progression.h"

using namespace dw;

TEST_CASE("xp curve and cap") {
  CHECK(xpForLevel(1) == 100);
  CHECK(xpForLevel(2) == 400);
  CHECK(xpForLevel(10) == 10000);
  CHECK(xpForLevel(50) == 0);
}

TEST_CASE("level fold is order independent") {
  // Applying the same XP in chunks equals folding the total once.
  int64_t total = 0;
  for (int i = 1; i <= 30; ++i) total += xpForLevel(i) + 37;
  LevelState chunked;
  int64_t remaining = total;
  while (remaining > 0) {
    int64_t step = std::min<int64_t>(remaining, 733);
    applyXp(chunked, step);
    remaining -= step;
  }
  XpResult once = levelFromTotal(total);
  CHECK(chunked.level == once.level);
  CHECK(chunked.xp == once.xp);
}

TEST_CASE("levels never exceed the cap and xp never goes negative") {
  XpResult r = levelFromTotal(1000000000);
  CHECK(r.level == 50);
  LevelState s;
  s.level = 5;
  s.xp = 10;
  applyXp(s, -50);
  CHECK(s.level == 5);
  CHECK(s.xp == 0);
}

TEST_CASE("titles and progression view") {
  CHECK(titleForLevel(1) == "Beginner");
  CHECK(titleForLevel(10) == "Builder");
  CHECK(titleForLevel(25) == "Systems Developer");
  CHECK(titleForLevel(50) == "Creator");
  JVal v = progressionView(450);
  CHECK(v.i("level") == 2);
  CHECK(v.i("totalXp") == 450);
  CHECK(v.i("xpIntoLevel") == 350);
  CHECK(v.i("xpForNextLevel") == 400);
}

TEST_CASE("mission gating over the completed set") {
  MissionDef m1, m2, m3;
  m1.id = "m01";
  m2.id = "m02";
  m2.requires = {"m01"};
  m3.id = "m03";
  m3.requires = {"m01", "m02"};
  std::map<std::string, MissionDef> missions = {{"m01", m1}, {"m02", m2}, {"m03", m3}};

  CHECK(requirementsMet(m1, {}));
  CHECK_FALSE(requirementsMet(m2, {}));
  CHECK(requirementsMet(m2, {"m01"}));
  CHECK_FALSE(requirementsMet(m3, {"m01"}));

  // Nothing completed: only the requirement-free mission is startable.
  auto open0 = eligibleUnlocks(missions, {});
  CHECK(open0.size() == 1);
  CHECK(open0[0] == "m01");

  // m01 done: m02 opens, m03 still gated on m02, m01 excluded.
  auto open1 = eligibleUnlocks(missions, {"m01"});
  CHECK(open1.size() == 1);
  CHECK(open1[0] == "m02");

  // Chain complete: nothing left to unlock.
  auto openAll = eligibleUnlocks(missions, {"m01", "m02", "m03"});
  CHECK(openAll.empty());
}
