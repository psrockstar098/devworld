// tests for src/portfolio.h — project portfolio and reputation evidence.
#include "doctest.h"
#include "portfolio.h"

using namespace dw;

static MissionDef mission(const std::string& id, int level, bool gate, int tests) {
  MissionDef m;
  m.id = id;
  m.title = "Project " + id;
  m.giver = "Nova";
  m.language = "python";
  m.level = level;
  m.gate = gate;
  m.xpReward = 100;
  for (int i = 0; i < tests; ++i) m.tests.push_back(TestCase{});
  return m;
}

TEST_CASE("the portfolio shows available and finished work, locked work hidden") {
  std::map<std::string, MissionDef> missions;
  missions["m01"] = mission("m01", 1, false, 2);
  missions["m02"] = mission("m02", 2, false, 3);
  missions["m03"] = mission("m03", 3, false, 3);
  missions["m04"] = mission("m04", 4, false, 3);

  std::map<std::string, MissionProgress> progress;
  progress["m01"].state = MissionState::Complete;
  progress["m02"].state = MissionState::Active;
  progress["m03"].state = MissionState::Locked;
  // m04 has no progress entry at all

  auto entries = Portfolio::build(missions, progress);
  REQUIRE(entries.size() == 2);              // m01 and m02 only
  CHECK(entries[0].id == "m01");             // completed sorts first
  CHECK(entries[0].completed);
  CHECK(entries[1].id == "m02");
  CHECK_FALSE(entries[1].completed);
  CHECK(entries[0].tests == 2);
}

TEST_CASE("evidence score weights gates and hidden tests") {
  auto e1 = PortfolioEntry{}; e1.completed = true; e1.tests = 3; e1.gate = false;
  auto e2 = PortfolioEntry{}; e2.completed = true; e2.tests = 3; e2.gate = true;
  auto e3 = PortfolioEntry{}; e3.completed = false; e3.tests = 9;  // unfinished: no credit
  std::vector<PortfolioEntry> v{e1, e2, e3};
  // 10 + 3*2  +  20 + 3*2  = 16 + 26 = 42
  CHECK(Portfolio::evidenceScore(v) == 42);
  CHECK(Portfolio::completedCount(v) == 2);
}

TEST_CASE("evidence tiers read as a reputation ladder") {
  CHECK(Portfolio::evidenceTier(0) == "Empty");
  CHECK(Portfolio::evidenceTier(1) == "Emerging");
  CHECK(Portfolio::evidenceTier(60) == "Solid");
  CHECK(Portfolio::evidenceTier(140) == "Strong");
  CHECK(Portfolio::evidenceTier(240) == "Exceptional");
}

TEST_CASE("entries sort completed first, then by level, then id") {
  std::map<std::string, MissionDef> missions;
  missions["a"] = mission("a", 5, false, 1);
  missions["b"] = mission("b", 2, false, 1);
  missions["c"] = mission("c", 2, false, 1);
  std::map<std::string, MissionProgress> progress;
  progress["a"].state = MissionState::Active;
  progress["b"].state = MissionState::Complete;
  progress["c"].state = MissionState::Available;

  auto entries = Portfolio::build(missions, progress);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].id == "b");  // completed first
  CHECK(entries[1].id == "c");  // then level 2 before level 5
  CHECK(entries[2].id == "a");
}
