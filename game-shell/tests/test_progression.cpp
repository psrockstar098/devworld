// tests for src/progression.h — XP curve, level caps, mission gating.
// These are the actual rules the game runs; the Game class only wraps them.
#include "doctest.h"
#include "progression.h"

using namespace dw;

static MissionDef mission(const std::string& id, std::vector<std::string> requires = {}) {
  MissionDef m;
  m.id = id;
  m.requires = std::move(requires);
  return m;
}

static MissionProgress state(MissionState s) {
  MissionProgress p;
  p.state = s;
  return p;
}

TEST_CASE("XP curve matches the Phase 1 design") {
  CHECK(xpForLevel(1) == 100);
  CHECK(xpForLevel(2) == 400);
  CHECK(xpForLevel(3) == 900);
  CHECK(xpForLevel(5) == 2500);
  CHECK(xpForLevel(10) == 10000);
}

TEST_CASE("XP curve is zero past the level cap") {
  CHECK(xpForLevel(0) == xpForLevel(1));
  CHECK(xpForLevel(kMaxLevel) == 0);
  CHECK(xpForLevel(999) == 0);
}

TEST_CASE("applyXp levels up once at the exact threshold") {
  LevelState l;
  XpResult r = applyXp(l, 100);
  CHECK(r.levelsGained == 1);
  CHECK(r.level == 2);
  CHECK(r.xp == 0);
  CHECK(l.level == 2);
  CHECK(l.xp == 0);
}

TEST_CASE("applyXp carries remainder and can gain several levels") {
  LevelState l;
  // 100 (L1) + 400 (L2) + 900 (L3) = 1400 gets to L4 with 0 left.
  XpResult r = applyXp(l, 1500);
  CHECK(r.levelsGained == 3);
  CHECK(r.level == 4);
  CHECK(r.xp == 100);
  CHECK(l.level == 4);
  CHECK(l.xp == 100);
}

TEST_CASE("applyXp does not level below the threshold") {
  LevelState l;
  XpResult r = applyXp(l, 99);
  CHECK(r.levelsGained == 0);
  CHECK(l.level == 1);
  CHECK(l.xp == 99);
}

TEST_CASE("applyXp never produces negative XP") {
  LevelState l;
  applyXp(l, 50);
  XpResult r = applyXp(l, -500);
  CHECK(r.level == 1);
  CHECK(r.xp == 0);
  CHECK(l.xp == 0);
}

TEST_CASE("applyXp stops at the level cap and keeps XP") {
  LevelState l;
  l.level = kMaxLevel - 1;
  l.xp = 0;
  XpResult r = applyXp(l, 1000000);
  CHECK(r.levelsGained == 1);
  CHECK(r.level == kMaxLevel);
  CHECK(l.level == kMaxLevel);
  CHECK(l.xp > 0);  // overflow XP is kept, not lost
}

TEST_CASE("requirementsMet handles empty, complete and missing prerequisites") {
  CHECK(requirementsMet(mission("m02"), {}));

  auto progress = std::map<std::string, MissionProgress>{
      {"m01", state(MissionState::Complete)}};
  CHECK(requirementsMet(mission("m02", {"m01"}), progress));
  CHECK_FALSE(requirementsMet(mission("m02", {"m01", "m99"}), progress));

  progress["m01"].state = MissionState::Active;
  CHECK_FALSE(requirementsMet(mission("m02", {"m01"}), progress));
}

TEST_CASE("eligibleUnlocks only returns locked missions whose gate is met") {
  std::map<std::string, MissionDef> missions{
      {"m01", mission("m01")},
      {"m02", mission("m02", {"m01"})},
      {"m03", mission("m03", {"m02"})},
      {"m04", mission("m04", {"m01"})},
  };
  auto progress = std::map<std::string, MissionProgress>{
      {"m01", state(MissionState::Complete)},
      {"m02", state(MissionState::Locked)},
      {"m03", state(MissionState::Locked)},
      {"m04", state(MissionState::Active)},
  };

  auto unlocked = eligibleUnlocks(missions, progress);
  REQUIRE(unlocked.size() == 1);
  CHECK(unlocked[0] == "m02");

  progress["m02"].state = MissionState::Complete;
  unlocked = eligibleUnlocks(missions, progress);
  REQUIRE(unlocked.size() == 1);
  CHECK(unlocked[0] == "m03");
}

TEST_CASE("eligibleUnlocks is sorted and ignores finished missions") {
  std::map<std::string, MissionDef> missions{
      {"m10", mission("m10", {"m01"})},
      {"m02", mission("m02", {"m01"})},
      {"m01", mission("m01")},
  };
  auto progress = std::map<std::string, MissionProgress>{
      {"m01", state(MissionState::Complete)},
      {"m02", state(MissionState::Complete)},
      {"m10", state(MissionState::Locked)},
  };
  auto unlocked = eligibleUnlocks(missions, progress);
  REQUIRE(unlocked.size() == 1);
  CHECK(unlocked[0] == "m10");
}

TEST_CASE("countActiveMissions includes Submitted, excludes Complete") {
  auto progress = std::map<std::string, MissionProgress>{
      {"a", state(MissionState::Active)},
      {"b", state(MissionState::Submitted)},
      {"c", state(MissionState::Complete)},
      {"d", state(MissionState::Available)},
      {"e", state(MissionState::Locked)},
  };
  CHECK(countActiveMissions(progress) == 2);
}

TEST_CASE("titles follow the ten progression tiers") {
  CHECK(titleForLevel(1) == "Beginner");
  CHECK(titleForLevel(5) == "Beginner");
  CHECK(titleForLevel(6) == "Builder");
  CHECK(titleForLevel(11) == "Engineer");
  CHECK(titleForLevel(16) == "Specialist");
  CHECK(titleForLevel(21) == "Systems Developer");
  CHECK(titleForLevel(26) == "Inventor");
  CHECK(titleForLevel(31) == "Architect");
  CHECK(titleForLevel(36) == "Innovator");
  CHECK(titleForLevel(41) == "Master Engineer");
  CHECK(titleForLevel(46) == "Creator");
  CHECK(titleForLevel(50) == "Creator");
}

TEST_CASE("wallet balance is derived from the ledger") {
  Player p;
  CHECK(p.balance() == 0);
  p.ledger.push_back({"1", "t", "grant", "starter", 20});
  p.ledger.push_back({"2", "t", "shop_buy", "noodles", -5});
  CHECK(p.balance() == 15);
  // No separate stored balance to drift out of sync.
  p.ledger.push_back({"3", "t", "mission_reward", "m01", 10});
  CHECK(p.balance() == 25);
}

TEST_CASE("objective text follows mission state and names the next action") {
  MissionDef m = mission("m07", {"m06"});
  m.title = "Stack the Deck";
  m.file = "missions/m07_stack.py";

  MissionProgress locked;
  locked.state = MissionState::Locked;
  CHECK(objectiveFor(m, locked) == "Locked — finish m06 first");

  MissionProgress avail;
  avail.state = MissionState::Available;
  CHECK(objectiveFor(m, avail) == "Accept at the mission board");

  MissionProgress active;
  active.state = MissionState::Active;
  std::string obj = objectiveFor(m, active);
  CHECK(obj.find("m07_stack.py") != std::string::npos);
  CHECK(obj.find("hidden") != std::string::npos);

  MissionProgress submitted;
  submitted.state = MissionState::Submitted;
  CHECK(objectiveFor(m, submitted).find("running") != std::string::npos);

  MissionProgress done;
  done.state = MissionState::Complete;
  CHECK(objectiveFor(m, done).find("Proof of Skill") != std::string::npos);
}

TEST_CASE("a locked mission with no prerequisites still reads sensibly") {
  MissionDef m = mission("m01");
  MissionProgress locked;
  locked.state = MissionState::Locked;
  CHECK(objectiveFor(m, locked) == "Locked");
}
