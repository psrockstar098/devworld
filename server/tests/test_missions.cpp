#include "doctest.h"

#include "missions.h"
#include "util.h"

using namespace dw;

static const char* kContent = R"({
  "missions": [
    {
      "id": "m01", "title": "First Boot", "language": "python", "level": 1,
      "requires": [], "xpReward": 150, "creditReward": 10, "gate": false,
      "tests": [
        {"name": "greets", "stdin": "", "expected": "Hello", "hidden": false},
        {"name": "edge", "stdin": "x", "expected": "y", "hidden": true}
      ]
    },
    {
      "id": "m02", "title": "Second", "language": "python", "level": 2,
      "requires": ["m01"], "xpReward": 300, "creditReward": 20, "gate": true,
      "tests": [{"name": "t", "stdin": "", "expected": "z", "hidden": false}]
    }
  ]
})";

TEST_CASE("mission catalogue loads and validates") {
  MissionCatalog cat;
  std::string err;
  REQUIRE(cat.loadFromJson(parseJson(kContent), &err));
  CHECK(cat.size() == 2);
  const MissionDef* m = cat.get("m01");
  REQUIRE(m != nullptr);
  CHECK(m->xpReward == 150);
  CHECK(m->tests.size() == 2);
  CHECK(cat.get("nope") == nullptr);

  JVal bad = parseJson(R"({"missions":[{"id":"x","tests":[]}]})");
  CHECK_FALSE(cat.loadFromJson(bad, &err));
}

TEST_CASE("public mission JSON hides hidden test data") {
  MissionCatalog cat;
  std::string err;
  REQUIRE(cat.loadFromJson(parseJson(kContent), &err));
  JVal j = cat.publicJson(*cat.get("m01"));
  CHECK(j.s("id") == "m01");
  CHECK(j.at("requires").type == JVal::ARR);
  const JVal& tests = j.at("tests");
  REQUIRE(tests.arr.size() == 2);
  CHECK(tests.arr[0].s("expected") == "Hello");
  CHECK(tests.arr[1].boolean("hidden"));
  CHECK(tests.arr[1].s("expected").empty());
  CHECK(tests.arr[1].s("stdin").empty());
}

TEST_CASE("grading summary requires all public and hidden tests") {
  MissionCatalog cat;
  std::string err;
  REQUIRE(cat.loadFromJson(parseJson(kContent), &err));
  const MissionDef& m = *cat.get("m01");

  AssessResult all;
  AssessTestResult a;
  a.name = "greets";
  a.passed = true;
  a.actual = "Hello";
  AssessTestResult b;
  b.name = "edge";
  b.passed = true;
  b.hidden = true;
  all.results = {a, b};
  MissionGrading g = summarize(m, all);
  CHECK(g.passed);
  CHECK(g.rewarded);
  CHECK(g.total == 2);
  CHECK(g.publicPassed == 1);
  CHECK(g.hiddenPassed == 1);
  CHECK(g.xpReward == 150);
  CHECK(g.creditReward == 10);

  AssessResult missing = all;
  missing.results.pop_back();
  MissionGrading g2 = summarize(m, missing);
  CHECK_FALSE(g2.passed);
  CHECK_FALSE(g2.rewarded);
  CHECK(g2.xpReward == 0);
  // Hidden result never carries actual output.
  CHECK_FALSE(g2.results.arr[1].s("actual").size());
}

TEST_CASE("mission formats validate and default") {
  MissionCatalog cat;
  std::string err;
  // All 12 formats load (ids are index-based: format names contain '-',
  // which mission ids do not allow).
  std::string json = "{\"missions\":[";
  for (size_t i = 0; i < missionFormats().size(); i++) {
    if (i) json += ",";
    json += "{\"id\":\"f" + std::to_string(i) + "\",\"format\":\"" +
            missionFormats()[i] +
            "\",\"tests\":[{\"name\":\"t\",\"stdin\":\"\",\"expected\":\"z\",\"hidden\":false}]}";
  }
  json += "]}";
  REQUIRE(cat.loadFromJson(parseJson(json), &err));
  CHECK(cat.size() == 12);
  CHECK(cat.get("f0")->format == "predict");
  CHECK(cat.get("f2")->format == "write-from-spec");
  CHECK(cat.get("f11")->format == "transfer-skill");
  // An unknown format is rejected, naming the bad value.
  JVal bad = parseJson(
      R"({"missions":[{"id":"x","format":"teleport","tests":[{"name":"t","stdin":"","expected":"z","hidden":false}]}]})");
  CHECK_FALSE(cat.loadFromJson(bad, &err));
  CHECK(err.find("teleport") != std::string::npos);
  // Absent format defaults to write-from-spec and shows in public JSON.
  REQUIRE(cat.loadFromJson(parseJson(kContent), &err));
  CHECK(cat.get("m01")->format == "write-from-spec");
  CHECK(cat.publicJson(*cat.get("m01")).s("format") == "write-from-spec");
}

TEST_CASE("the shipped content loads") {
  MissionCatalog cat;
  std::string err;
  if (!cat.loadFromFile("../game-shell/content/missions.json", &err)) {
    MESSAGE("skipping shipped content: " << err);
    return;
  }
  CHECK(cat.size() == 23);
  CHECK(cat.get("m01") != nullptr);
  // Every shipped mission is still the default format (no content changed).
  for (const auto& kv : cat.all())
    CHECK(kv.second.format == "write-from-spec");
  // Every referenced prerequisite exists.
  for (const auto& kv : cat.all()) {
    for (const auto& r : kv.second.requires) CHECK(cat.get(r) != nullptr);
  }
}
