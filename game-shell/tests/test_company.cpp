// tests for src/company.h — founding and running a company (convenience only).
#include "doctest.h"
#include "company.h"

using namespace dw;

TEST_CASE("company names are validated") {
  std::string why;
  CHECK(Company::validName("Blue Pixel"));
  CHECK(Company::validName("abc"));
  CHECK(Company::validName("Studio_9"));
  CHECK_FALSE(Company::validName("ab", &why));            // too short
}

TEST_CASE("name rules reject empties, symbols and letter-less names") {
  std::string why;
  CHECK_FALSE(Company::validName("", &why));
  CHECK_FALSE(Company::validName("this name is definitely too long", &why));
  CHECK_FALSE(Company::validName("Bad-Name!", &why));
  CHECK_FALSE(Company::validName("123_456", &why));       // no letter
  CHECK(why.find("letter") != std::string::npos);
}

TEST_CASE("focus areas are a fixed, known list") {
  const auto& f = Company::focuses();
  CHECK(f.size() == 4);
  CHECK(Company::validFocus("Research Group"));
  CHECK_FALSE(Company::validFocus("Crypto Farm"));
  CHECK_FALSE(Company::focusBlurb("Research Group").empty());
  CHECK_FALSE(Company::focusBlurb("unknown").empty());  // always something to say
}

TEST_CASE("you found a company once") {
  Player p;
  std::string why;
  CHECK_FALSE(Company::found(p, "ab", "Product Lab", &why));
  CHECK_FALSE(Company::found(p, "Good Name", "Nope", &why));
  CHECK_FALSE(p.company.founded);

  REQUIRE(Company::found(p, "Blue Pixel", "Infra Shop", &why));
  CHECK(p.company.founded);
  CHECK(p.company.name == "Blue Pixel");
  CHECK(p.company.focus == "Infra Shop");

  CHECK_FALSE(Company::found(p, "Second Try", "Product Lab", &why));
  CHECK(why.find("already run") != std::string::npos);
  CHECK(p.company.name == "Blue Pixel");  // unchanged
}

TEST_CASE("rank climbs with reputation") {
  CHECK(Company::rankFor(0) == "Two Desks and a Kettle");
  CHECK(Company::rankFor(20) == "Garage Studio");
  CHECK(Company::rankFor(80) == "Growing Studio");
  CHECK(Company::rankFor(200) == "Established Agency");
  CHECK(Company::rankFor(1000) == "Industry Name");
}

TEST_CASE("company reputation is derived from real completed work") {
  std::map<std::string, MissionProgress> m;
  m["m01"].state = MissionState::Complete;
  m["m02"].state = MissionState::Complete;
  m["m03"].state = MissionState::Active;   // not finished: no credit
  CHECK(Company::reputationFrom(m, 0) == 10);
  CHECK(Company::reputationFrom(m, 2) == 16);  // +3 per contract
}

TEST_CASE("contract payout is bounded convenience money") {
  CHECK(Company::contractPayout(1000, 0, 1) == 1000);      // no bonus yet
  CHECK(Company::contractPayout(1000, 100, 1) == 1100);    // +10%
  CHECK(Company::contractPayout(1000, 0, 3) == 1200);      // +10% per extra member
  CHECK(Company::contractPayout(1000, 100000, 9) == 2500); // capped at +150%
  CHECK(Company::contractPayout(1, 0, 1) == 1);
}

TEST_CASE("company info summarises the record") {
  Player p;
  CHECK_FALSE(Company::info(p).founded);
  CHECK(Company::info(p).name.empty());

  Company::found(p, "Blue Pixel", "Product Lab");
  p.company.reputation = 85;
  p.company.contracts = 4;
  p.company.members = {"nova", "ada"};
  CompanyInfo i = Company::info(p);
  CHECK(i.name == "Blue Pixel");
  CHECK(i.rank == "Growing Studio");
  CHECK(i.members == 2);
  CHECK_FALSE(i.focusBlurb.empty());
}
