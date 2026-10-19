// tests for src/json.h — the content-file parser. It is small, but a parser
// bug silently corrupts missions, so it gets its own suite.
#include "doctest.h"
#include "json.h"

using dw::parseJson;

TEST_CASE("parses objects, arrays and scalars") {
  auto j = parseJson(R"({"name":"Junie","level":3,"ok":true,"nothing":null,"xs":[1,2,3]})");
  CHECK(j.type == dw::JVal::OBJ);
  CHECK(j.s("name") == "Junie");
  CHECK(j.i("level") == 3);
  CHECK(j.boolean("ok"));
  CHECK(j.at("nothing").isNull());
  REQUIRE(j.at("xs").arr.size() == 3);
  CHECK(j.at("xs").at(1).num == 2);
}

TEST_CASE("missing keys return safe defaults") {
  auto j = parseJson("{}");
  CHECK(j.s("nope") == "");
  CHECK(j.n("nope", 42) == 42);
  CHECK(j.i("nope", 7) == 7);
  CHECK_FALSE(j.boolean("nope"));
  CHECK(j.at("nope").isNull());
  CHECK(j.at(99).isNull());
}

TEST_CASE("parses string escapes") {
  auto j = parseJson(R"({"s":"line\nbreak\ttab \"quoted\" \\ end","u":"\u00e9"})");
  CHECK(j.s("s") == "line\nbreak\ttab \"quoted\" \\ end");
  CHECK(j.s("u") == "\xC3\xA9");  // é as UTF-8
}

TEST_CASE("parses numbers, including negatives and exponents") {
  auto j = parseJson(R"({"a":-12,"b":0.5,"c":1e3,"d":-2.5e-2})");
  CHECK(j.n("a") == -12);
  CHECK(j.n("b") == 0.5);
  CHECK(j.n("c") == 1000);
  CHECK(j.n("d") == doctest::Approx(-0.025));
}

TEST_CASE("malformed JSON throws instead of returning garbage") {
  CHECK_THROWS(parseJson(R"({"a":})"));
  CHECK_THROWS(parseJson("{\"a\":1"));
  CHECK_THROWS(parseJson("[1,2"));
  CHECK_THROWS(parseJson(""));
}
