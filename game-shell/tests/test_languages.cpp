// tests/test_languages.cpp — the language catalog loader and validator.
#include "doctest.h"
#include "json.h"
#include "languages.h"

using namespace dw;

TEST_CASE("languages load and validate") {
  Languages ls;
  ls.load(parseJson(R"({
    "languages": [
      {"id": "python", "name": "Python", "tier": "runs-now",
       "blurb": "All-rounder", "disciplines": ["full_stack"]},
      {"id": "c", "name": "C", "tier": "sandbox",
       "blurb": "Machine", "disciplines": ["os_distributed"]}
    ]})"));
  CHECK(ls.validate().empty());
  REQUIRE(ls.all().size() == 2);
  CHECK(ls.def("python") != nullptr);
  CHECK(ls.def("python")->disciplines.size() == 1);
  CHECK(ls.def("nope") == nullptr);
}

TEST_CASE("language validation rejects bad entries") {
  Languages ls;
  ls.load(parseJson(R"({
    "languages": [
      {"id": "a", "name": "A", "tier": "runs-now",
       "disciplines": ["full_stack"]},
      {"id": "a", "name": "Dup", "tier": "runs-now",
       "disciplines": ["full_stack"]},
      {"id": "b", "name": "", "tier": "runs-now",
       "disciplines": ["full_stack"]},
      {"id": "c", "name": "C", "tier": "teleport",
       "disciplines": ["full_stack"]},
      {"id": "d", "name": "D", "tier": "runs-now",
       "disciplines": []}
    ]})"));
  // Duplicate id, empty name, unknown tier, no discipline.
  CHECK(ls.validate().size() >= 4);
}
