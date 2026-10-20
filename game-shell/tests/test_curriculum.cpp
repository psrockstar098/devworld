// tests/test_curriculum.cpp — the knowledge graph (design 4.1).
//
// These tests use small in-code fixtures so the graph algorithms are exercised
// without depending on how much content has shipped. tests/test_content.cpp
// separately validates the real content/curriculum.json.
#include "doctest.h"
#include "curriculum.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace dw;

namespace {

// Two areas, four topics in a diamond: t1 -> t2 -> t3, t1 -> t4.
//   area a1: t1 (easy), t2 (hard)
//   area a2: t3, t4
const char* kFixture = R"({
  "areas": [
    {"id":"a1","title":"Area One","summary":"first"},
    {"id":"a2","title":"Area Two","summary":"second"}
  ],
  "topics": [
    {"id":"t1","area":"a1","title":"T1","difficulty":1,
     "objectives":["know t1"],
     "missions":["m01"]},
    {"id":"t2","area":"a1","title":"T2","difficulty":3,
     "prerequisites":["t1"],
     "objectives":[{"id":"t2.o1","text":"know t2"}],
     "misconceptions":[{"id":"t2.mc1","text":"wrong","remedy":"right","probes":["m02"]}],
     "missions":["m02"]},
    {"id":"t3","area":"a2","title":"T3","difficulty":2,
     "prerequisites":["t2"],
     "objectives":["know t3"],
     "missions":["m03"]},
    {"id":"t4","area":"a2","title":"T4","difficulty":1,
     "prerequisites":["t1"],
     "objectives":["know t4"],
     "projects":["capstone t4"]}
  ]
})";

Curriculum fixture() {
  Curriculum c;
  c.load(parseJson(kFixture));
  return c;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

int indexOf(const std::vector<std::string>& v, const std::string& s) {
  auto it = std::find(v.begin(), v.end(), s);
  return it == v.end() ? -1 : (int)(it - v.begin());
}

}  // namespace

TEST_CASE("curriculum loads areas and topics") {
  Curriculum c = fixture();
  CHECK_FALSE(c.empty());
  REQUIRE(c.areas().size() == 2);
  CHECK(c.areas()[0].id == "a1");
  CHECK(c.area("a2") != nullptr);
  CHECK(c.area("nope") == nullptr);

  REQUIRE(c.topics().size() == 4);
  const Topic* t = c.topic("t2");
  REQUIRE(t != nullptr);
  CHECK(t->area == "a1");
  CHECK(t->difficulty == 3);
  REQUIRE(t->objectives.size() == 1);
  CHECK(t->objectives[0].id == "t2.o1");  // explicit id preserved
  REQUIRE(t->misconceptions.size() == 1);
  CHECK(t->misconceptions[0].remedy == "right");
  // A plain-string objective gets a generated, stable id.
  CHECK(c.topic("t1")->objectives[0].id == "t1.1");
}

TEST_CASE("topicsInArea sorts by difficulty then id") {
  Curriculum c = fixture();
  auto in = c.topicsInArea("a1");
  REQUIRE(in.size() == 2);
  CHECK(in[0]->id == "t1");  // difficulty 1
  CHECK(in[1]->id == "t2");  // difficulty 3
}

TEST_CASE("directPrerequisites drops unknown ids") {
  Curriculum c = fixture();
  CHECK(c.directPrerequisites("t1").empty());
  auto deps = c.directPrerequisites("t3");
  REQUIRE(deps.size() == 1);
  CHECK(deps[0] == "t2");
  CHECK(c.directPrerequisites("missing").empty());
}

TEST_CASE("allPrerequisites is transitive and dependency-first") {
  Curriculum c = fixture();
  auto need = c.allPrerequisites("t3");
  REQUIRE(need.size() == 2);
  CHECK(indexOf(need, "t1") < indexOf(need, "t2"));  // t1 before t2
  CHECK_FALSE(contains(need, "t3"));                 // excludes the topic itself
  CHECK(c.allPrerequisites("t1").empty());
}

TEST_CASE("pathTo is prerequisites followed by the topic") {
  Curriculum c = fixture();
  auto path = c.pathTo("t3");
  REQUIRE(path.size() == 3);
  CHECK(path.back() == "t3");
  CHECK(indexOf(path, "t1") < indexOf(path, "t2"));
  CHECK(c.pathTo("nope").empty());
}

TEST_CASE("topologicalOrder puts every prerequisite before its dependents") {
  Curriculum c = fixture();
  auto order = c.topologicalOrder();
  REQUIRE(order.size() == 4);
  CHECK(indexOf(order, "t1") < indexOf(order, "t2"));
  CHECK(indexOf(order, "t2") < indexOf(order, "t3"));
  CHECK(indexOf(order, "t1") < indexOf(order, "t4"));
}

TEST_CASE("availableTopics unlocks only satisfied topics, in area order") {
  Curriculum c = fixture();
  auto none = c.availableTopics({});
  REQUIRE(none.size() == 1);
  CHECK(none[0] == "t1");  // the only root

  auto afterT1 = c.availableTopics({"t1"});
  REQUIRE(afterT1.size() == 2);
  CHECK(contains(afterT1, "t2"));
  CHECK(contains(afterT1, "t4"));
  // a1 comes before a2, so t2 precedes t4 even though t4 is easier.
  CHECK(afterT1[0] == "t2");
  CHECK(afterT1[1] == "t4");

  auto all = c.availableTopics({"t1", "t2", "t3", "t4"});
  CHECK(all.empty());  // mastered topics are not offered again
}

TEST_CASE("recommendedNext honours the limit") {
  Curriculum c = fixture();
  auto one = c.recommendedNext({"t1"}, 1);
  REQUIRE(one.size() == 1);
  CHECK(one[0] == "t2");
  CHECK(c.recommendedNext({"t1"}, 10).size() == 2);
}

TEST_CASE("topicsForMission is the reverse index") {
  Curriculum c = fixture();
  auto t = c.topicsForMission("m02");
  REQUIRE(t.size() == 1);
  CHECK(t[0] == "t2");
  CHECK(c.topicsForMission("m99").empty());
}

TEST_CASE("validate accepts a sound graph") {
  Curriculum c = fixture();
  auto errs = c.validate({"m01", "m02", "m03"});
  for (const auto& e : errs) INFO(e);
  CHECK(errs.empty());
  // Mission checking can be skipped for unit fixtures.
  CHECK(c.validate().empty());
}

TEST_CASE("validate catches structural content errors") {
  Curriculum c;
  c.load(parseJson(R"({
    "areas": [
      {"id":"a1","title":"A","summary":"s"},
      {"id":"a1","title":"dup","summary":"s"},
      {"id":"","title":"","summary":""}
    ],
    "topics": [
      {"id":"bad","area":"ghost","title":"","difficulty":9,
       "prerequisites":["bad","missing"],
       "objectives":["ok"],
       "misconceptions":[{"id":"mc","text":"","remedy":"","probes":["mXX"]}],
       "missions":["mXX"]}
    ]
  })"));
  auto errs = c.validate({"m01"});
  for (const auto& e : errs) INFO(e);
  CHECK(contains(errs, "duplicate area: a1"));
  CHECK(contains(errs, "topic bad: unknown area ghost"));
  CHECK(contains(errs, "topic bad: missing title"));
  CHECK(contains(errs, "topic bad: difficulty must be 1..5"));
  CHECK(contains(errs, "topic bad: depends on itself"));
  CHECK(contains(errs, "topic bad: unknown prerequisite missing"));
  CHECK(contains(errs, "misconception mc: missing text"));
  CHECK(contains(errs, "misconception mc: missing remedy"));
  CHECK(contains(errs, "misconception mc: unknown probe mission mXX"));
  CHECK(contains(errs, "topic bad: unknown mission mXX"));
}

TEST_CASE("validate requires a practical project or mission") {
  Curriculum c;
  c.load(parseJson(R"({
    "areas":[{"id":"a","title":"A","summary":"s"}],
    "topics":[{"id":"t","area":"a","title":"T","objectives":["o"]}]
  })"));
  auto errs = c.validate();
  for (const auto& e : errs) INFO(e);
  CHECK(contains(errs, "topic t: no practical project or mission"));
}

TEST_CASE("validate detects cycles and names a path") {
  Curriculum c;
  c.load(parseJson(R"({
    "areas":[{"id":"a","title":"A","summary":"s"}],
    "topics":[
      {"id":"x","area":"a","title":"X","prerequisites":["y"],"objectives":["o"],"projects":["p"]},
      {"id":"y","area":"a","title":"Y","prerequisites":["x"],"objectives":["o"],"projects":["p"]}
    ]
  })"));
  auto errs = c.validate();
  bool cycle = false;
  for (const auto& e : errs)
    if (e.rfind("cycle:", 0) == 0) cycle = true;
  CHECK(cycle);
  // A cyclic graph still produces a total order for callers that ask for one.
  CHECK(c.topologicalOrder().size() == 2);
}

TEST_CASE("reloading replaces the previous graph") {
  Curriculum c = fixture();
  c.load(parseJson(R"({
    "areas":[{"id":"z","title":"Z","summary":"s"}],
    "topics":[{"id":"z1","area":"z","title":"Z1","objectives":["o"],"projects":["p"]}]
  })"));
  CHECK(c.topics().size() == 1);
  CHECK(c.topic("t1") == nullptr);
  CHECK(c.topic("z1") != nullptr);
}
