// test_capability.cpp — the separate progression records and the derived
// PlayerCapability snapshot (objective 5.1/5.3). The point of these tests is
// that *level is not expertise*: a level-40 player with no evidence has no
// mastery, and the snapshot carries every record a separate field.
#include "doctest.h"
#include "capability.h"

#include <map>
#include <string>
#include <vector>

using namespace dw;

static MissionDef mission(const std::string& id, const std::string& lang, int level, int tests,
                          bool gate = false) {
  MissionDef m;
  m.id = id;
  m.language = lang;
  m.level = level;
  m.gate = gate;
  for (int i = 0; i < tests; i++) m.tests.push_back(TestCase{"t", "", ""});
  return m;
}

TEST_CASE("reliability is derived from outcomes and bounded") {
  ReliabilityRecord empty;
  ReliabilityRecord good;
  good.passes = 10;
  good.fails = 2;
  good.recalls = 3;
  ReliabilityRecord flaky = good;
  flaky.fails = 1000;

  CHECK(reliabilityScore(empty) == 0);
  CHECK(reliabilityScore(good) > 0);
  CHECK(reliabilityScore(good) <= 100);
  CHECK(reliabilityScore(good) > reliabilityScore(flaky));
  CHECK(reliabilityScore(flaky) >= 0);
}

TEST_CASE("language proficiency grows with completed missions in that language") {
  std::map<std::string, MissionDef> ms;
  ms["a"] = mission("a", "python", 3, 2);
  ms["b"] = mission("b", "python", 5, 3);
  ms["c"] = mission("c", "sql", 4, 2);
  std::map<std::string, MissionProgress> prog;
  prog["a"].state = MissionState::Complete;
  prog["b"].state = MissionState::Complete;
  prog["c"].state = MissionState::Complete;
  std::vector<Evidence> evs;
  Curriculum cur;

  LanguageProficiency py = languageProficiency("python", ms, prog, evs, cur);
  LanguageProficiency sq = languageProficiency("sql", ms, prog, evs, cur);

  CHECK(py.missions == 2);
  CHECK(py.tests == 5);
  CHECK(py.tier > sq.tier);
  CHECK(sq.tier >= 1);
  CHECK(languageProficiency("rust", ms, prog, evs, cur).tier == 0);
}

TEST_CASE("capability separates mastery from level and keeps career history") {
  std::map<std::string, MissionDef> ms;
  ms["m13"] = mission("m13", "python", 5, 4, /*gate=*/true);
  std::map<std::string, MissionProgress> prog;
  prog["m13"].state = MissionState::Complete;

  Player p;
  p.levels.level = 40;  // breadth, earned by XP
  p.career.path = "systems";
  p.career.history = {"python_backend", "systems"};
  p.missions = prog;

  MasteryConfig cfg;
  PlayerCapability cap = deriveCapability(p, ms, Curriculum{}, cfg, 1000);

  CHECK(cap.level == 40);
  CHECK(cap.masteredSkills.empty());   // a high level grants no mastery
  CHECK(cap.independentSkills.empty());
  CHECK(cap.completedGates.size() == 1);
  CHECK(cap.completedGates[0] == "m13");
  CHECK(cap.career.history.size() == 2);  // history survives respecialization
}

TEST_CASE("capability counts independent evidence and derives a reliability tier") {
  std::map<std::string, MissionDef> ms;
  ms["m05"] = mission("m05", "python", 5, 2);

  Player p;
  p.reliability.passes = 4;
  p.reliability.fails = 1;
  p.evidence.push_back(makeEvidence("p", "loops.accumulators", EvidenceKind::Transfer, 1.0, 10, "m13"));
  p.evidence.push_back(makeEvidence("p", "loops.accumulators", EvidenceKind::AutomatedTest, 1.0, 11, "m02"));

  MasteryConfig cfg;
  PlayerCapability cap = deriveCapability(p, ms, Curriculum{}, cfg, 100);

  CHECK(cap.exercisedSkills.size() == 1);
  CHECK(cap.independentCount >= 1);
  CHECK(cap.reliability > 0);
  CHECK_FALSE(cap.reliabilityTier.empty());
}
