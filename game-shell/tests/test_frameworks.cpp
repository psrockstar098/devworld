// tests/test_frameworks.cpp — the framework catalog loader and validator.
#include "doctest.h"
#include "frameworks.h"
#include "json.h"

using namespace dw;

TEST_CASE("frameworks load and validate") {
  Frameworks fw;
  fw.load(parseJson(R"({
    "disciplines": [{"id": "full_stack", "name": "Full-stack"}],
    "frameworks": [
      {"id": "react", "name": "React", "language": "javascript",
       "discipline": "full_stack", "blurb": "UI",
       "skills": ["functions.decomposition"]},
      {"id": "docker", "name": "Docker", "language": "",
       "discipline": "full_stack", "blurb": "Containers"}
    ]})"));
  CHECK(fw.validate().empty());
  REQUIRE(fw.all().size() == 2);
  CHECK(fw.def("react") != nullptr);
  CHECK(fw.def("react")->skills.size() == 1);
  CHECK(fw.def("docker") != nullptr);
  CHECK(fw.def("docker")->language.empty());
  CHECK(fw.def("nope") == nullptr);
  CHECK(fw.disciplines().count("full_stack") == 1);
}

TEST_CASE("framework validation rejects bad entries") {
  Frameworks fw;
  fw.load(parseJson(R"({
    "disciplines": [{"id": "full_stack", "name": "Full-stack"}],
    "frameworks": [
      {"id": "a", "name": "A", "language": "python",
       "discipline": "full_stack"},
      {"id": "a", "name": "Dup", "language": "python",
       "discipline": "full_stack"},
      {"id": "b", "name": "", "language": "python",
       "discipline": "full_stack"},
      {"id": "c", "name": "C", "language": "python",
       "discipline": "nowhere"},
      {"id": "d", "name": "D", "language": "python",
       "discipline": "full_stack", "level": -1}
    ]})"));
  // Duplicate id, empty name, unlisted discipline, negative level.
  CHECK(fw.validate().size() >= 4);
}
