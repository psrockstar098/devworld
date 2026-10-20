// test_careers.cpp — career paths and respecialization (objective 5.5).
#include "doctest.h"
#include "careers.h"

using namespace dw;

TEST_CASE("respecializing keeps earned competencies") {
  Careers cs;
  cs.load(parseJson(R"({"paths":[
    {"id":"python_backend","title":"Backend","language":"python","entry":{}},
    {"id":"systems","title":"Systems","language":"c","entry":{}}]})"));

  Player p;
  p.evidence.push_back(
      makeEvidence("p", "loops.for_iteration", EvidenceKind::Transfer, 1.0, 1, "m13"));
  p.missions["m01"].state = MissionState::Complete;
  p.achievements.push_back("first_steps");

  PlayerCapability cap;  // empty entries are always satisfied
  std::string why;
  CHECK(cs.choose(p, *cs.def("python_backend"), cap, nullptr, nullptr, &why));
  CHECK(cs.choose(p, *cs.def("systems"), cap, nullptr, nullptr, &why));

  CHECK(p.career.path == "systems");
  CHECK(p.career.history.size() == 2);
  CHECK(p.career.history[0] == "python_backend");
  CHECK(p.career.history[1] == "systems");
  // Nothing was deleted by the switch.
  CHECK(p.evidence.size() == 1);
  CHECK(p.missions.at("m01").state == MissionState::Complete);
  CHECK(p.achievements.size() == 1);
}

TEST_CASE("choosing the same path twice does not duplicate history") {
  Careers cs;
  cs.load(parseJson(R"({"paths":[
    {"id":"toolsmith","title":"Toolsmith","language":"python","entry":{}}]})"));
  Player p;
  PlayerCapability cap;
  CHECK(cs.choose(p, *cs.def("toolsmith"), cap));
  CHECK(cs.choose(p, *cs.def("toolsmith"), cap));
  CHECK(p.career.history.size() == 1);
}

TEST_CASE("an unmet career entry refuses and explains why") {
  Careers cs;
  cs.load(parseJson(R"({"paths":[
    {"id":"systems","title":"Systems","language":"c","entry":{"level":10}}]})"));
  Player p;
  PlayerCapability cap;  // level 1
  std::string why;
  CHECK_FALSE(cs.choose(p, *cs.def("systems"), cap, nullptr, nullptr, &why));
  CHECK_FALSE(why.empty());
  CHECK(p.career.path.empty());
  CHECK(p.career.history.empty());
}

TEST_CASE("career eligibility reports reasons like any unlock") {
  Careers cs;
  cs.load(parseJson(R"({"paths":[
    {"id":"systems","title":"Systems","language":"c","entry":{"level":10}}]})"));
  PlayerCapability cap;
  cap.level = 10;
  auto st = cs.eligibility(cap);
  REQUIRE(st.size() == 1);
  CHECK(st[0].unlocked);
  CHECK(st[0].met.size() == 1);
}

TEST_CASE("validate rejects duplicates and missing fields") {
  Careers cs;
  cs.load(parseJson(R"({"paths":[
    {"id":"a","title":"A","language":"python","entry":{}},
    {"id":"a","title":"Dup","language":"python","entry":{}},
    {"id":"b","title":"","language":"python","entry":{}},
    {"id":"c","title":"C","entry":{}}]})"));
  CHECK(cs.validate().size() >= 3);
}
