// test_capability.cpp — the PlayerCapability record and its pure helpers.
// The point of these tests is that *level is not expertise*: the record
// carries every progression axis as a separate field, and reliability is
// derived from outcomes, bounded, and never a default.
#include "doctest.h"
#include "capability.h"

using namespace dw;

TEST_CASE("reliability is derived from outcomes and bounded") {
  ReliabilityRecord empty;
  ReliabilityRecord good;
  good.passes = 10;
  good.fails = 2;
  good.recalls = 3;
  ReliabilityRecord flaky = good;
  flaky.fails = 1000;

  CHECK(reliabilityScore(empty) == 0);
  CHECK(reliabilityScore(good) > 0);
  CHECK(reliabilityScore(good) <= 100);
  CHECK(reliabilityScore(good) > reliabilityScore(flaky));
  CHECK(reliabilityScore(flaky) >= 0);
}

TEST_CASE("reliability tiers split at the documented boundaries") {
  CHECK(reliabilityTier(0) == "unproven");
  CHECK(reliabilityTier(39) == "developing");
  CHECK(reliabilityTier(40) == "reliable");
  CHECK(reliabilityTier(69) == "reliable");
  CHECK(reliabilityTier(70) == "trusted");
  CHECK(reliabilityTier(89) == "trusted");
  CHECK(reliabilityTier(90) == "flawless");
  CHECK(reliabilityTier(100) == "flawless");
}

TEST_CASE("proficiency labels cover every tier") {
  CHECK(proficiencyLabel(0) == "none");
  CHECK(proficiencyLabel(1) == "novice");
  CHECK(proficiencyLabel(2) == "apprentice");
  CHECK(proficiencyLabel(3) == "competent");
  CHECK(proficiencyLabel(4) == "proficient");
  CHECK(proficiencyLabel(5) == "expert");
}

TEST_CASE("a brand-new capability carries no mastery") {
  PlayerCapability cap;
  CHECK(cap.level == 1);
  CHECK(cap.masteredSkills.empty());
  CHECK(cap.independentSkills.empty());
  CHECK(cap.completedMissions.empty());
  CHECK(cap.has(cap.completedMissions, "m01") == false);
  cap.completedMissions = {"m01", "m02"};
  CHECK(cap.has(cap.completedMissions, "m02"));
}
