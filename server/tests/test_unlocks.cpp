// test_unlocks.cpp — the unlock catalog and prerequisite engine (Stage 5).
// The load-bearing claims: an empty requirement is open with a reason; a level
// alone never satisfies a technical privilege; a privilege may only require its
// own knowledge area; city access never requires a specific skill. Mirrors the
// shell's test_unlocks.cpp, plus the real shipped catalog.
#include "doctest.h"
#include "unlocks.h"

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

TEST_CASE("level alone does satisfy a breadth unlock") {
  UnlockDef d;
  d.id = "city";
  d.kind = UnlockKind::CityAccess;
  d.requires.minLevel = 5;
  PlayerCapability cap;
  cap.level = 50;  // breadth is exactly what city access asks for
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK(s.unlocked);
  CHECK(s.missing.empty());
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

TEST_CASE("evaluateAll returns every unlock sorted by id") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"zeta","kind":"city_access","requires":{"level":1}},
    {"id":"alpha","kind":"city_access","requires":{"level":1}}]})"));
  PlayerCapability cap;
  cap.level = 5;
  auto all = c.evaluateAll(cap);
  REQUIRE(all.size() == 2);
  CHECK(all[0].id == "alpha");
  CHECK(all[1].id == "zeta");
  CHECK(all[0].unlocked);
  CHECK(all[1].unlocked);
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

TEST_CASE("validate rejects duplicate unlock ids") {
  UnlockCatalog c;
  c.load(parseJson(R"({"unlocks":[
    {"id":"dup","kind":"city_access","requires":{"level":1}},
    {"id":"dup","kind":"city_access","requires":{"level":2}}]})"));
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

TEST_CASE("the shipped unlock catalog loads and validates") {
  std::string text = readFile("../game-shell/content/unlocks.json");
  if (text.empty()) {
    MESSAGE("skipping shipped unlocks: file not found");
    return;
  }
  UnlockCatalog c;
  c.load(parseJson(text));
  CHECK_FALSE(c.empty());
  CHECK(c.all().size() == 14);
  CHECK(c.milestones().size() == 4);
  auto errs = c.validate();
  for (const auto& e : errs) INFO(e);
  CHECK(errs.empty());
  // The first shipped unlock is breadth-only and a level-1 player earns it.
  const UnlockDef* starter = c.def("city.starter_town");
  REQUIRE(starter != nullptr);
  PlayerCapability cap;
  cap.level = 1;
  CHECK(c.evaluate(*starter, cap).unlocked);
}
