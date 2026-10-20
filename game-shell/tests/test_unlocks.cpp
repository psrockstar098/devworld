// test_unlocks.cpp — the unlock catalog and prerequisite engine (objective 5.3).
// The load-bearing claims: an empty requirement is open with a reason; a level
// alone never satisfies a technical privilege; a privilege may only require its
// own knowledge area; city access never requires a specific skill.
#include "doctest.h"
#include "unlocks.h"

using namespace dw;

TEST_CASE("an unlock with no requirements is open with a reason") {
  UnlockDef d;
  d.id = "starter";
  d.kind = UnlockKind::CityAccess;
  PlayerCapability cap;  // a brand-new player
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK(s.unlocked);
  CHECK(s.met.size() >= 1);
  CHECK(s.missing.empty());
}

TEST_CASE("level alone does not satisfy a technical privilege") {
  UnlockDef d;
  d.id = "tp";
  d.kind = UnlockKind::TechnicalPrivilege;
  d.area = "data";
  d.requires.independentSkills = {"data.mapping_keys"};
  PlayerCapability cap;
  cap.level = 50;  // breadth only
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK_FALSE(s.unlocked);
  CHECK(s.missing.size() == 1);
  CHECK(s.met.empty());
}

TEST_CASE("a satisfied technical privilege explains why") {
  UnlockDef d;
  d.id = "tp";
  d.kind = UnlockKind::TechnicalPrivilege;
  d.area = "data";
  d.requires.independentSkills = {"data.mapping_keys"};
  PlayerCapability cap;
  cap.level = 50;
  cap.independentSkills = {"data.mapping_keys"};
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK(s.unlocked);
  CHECK(s.met.size() == 1);
  CHECK(s.missing.empty());
}

TEST_CASE("validate rejects a privilege that requires an unrelated area") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"bad","kind":"technical_privilege","area":"data",
     "requires":{"independentSkills":["control_flow.boolean_logic"]}}]})"));
  CHECK_FALSE(c.validate().empty());
}

TEST_CASE("validate rejects a privilege gated on level alone") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"levelonly","kind":"technical_privilege","area":"tooling",
     "requires":{"level":10}}]})"));
  CHECK_FALSE(c.validate().empty());
}

TEST_CASE("validate rejects city access that requires a skill") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"city","kind":"city_access","requires":{"level":5,"skills":["loops.for_iteration"]}}]})"));
  CHECK_FALSE(c.validate().empty());
}

TEST_CASE("milestones resolve to a mission and are earned by completing it") {
  UnlockCatalog c;
  c.load(parseJson(R"({
    "milestones":[{"id":"first_proof","title":"First Proof","mission":"m13"}],
    "unlocks":[{"id":"bus","kind":"city_access",
                "requires":{"milestones":["first_proof"]}}]})"));
  CHECK(c.validate().empty());

  PlayerCapability cap;
  CHECK_FALSE(c.evaluate(*c.def("bus"), cap).unlocked);
  cap.completedMissions = {"m13"};
  CHECK(c.evaluate(*c.def("bus"), cap).unlocked);
}

TEST_CASE("validate rejects an unknown milestone") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"x","kind":"city_access","requires":{"milestones":["nope"]}}]})"));
  CHECK_FALSE(c.validate().empty());
}
