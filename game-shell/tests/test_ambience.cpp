// tests for src/ambience.h — the environment mix follows day/night and place.
#include "doctest.h"
#include "ambience.h"

using namespace dw;

static bool inRange(float v) { return v >= 0.0f && v <= 1.0f; }

TEST_CASE("all ambience levels stay in [0,1] across the day and every city") {
  const char* cities[] = {"starter_town", "startup_city", "systems_district",
                          "innovation_capital"};
  for (int i = 0; i <= 100; ++i) {
    float dayT = i / 100.0f;
    for (bool interior : {false, true}) {
      for (const char* c : cities) {
        AmbienceLevels a = ambienceFor(dayT, interior, c);
        INFO("dayT=" << dayT << " interior=" << interior << " city=" << c);
        CHECK(inRange(a.wind));
        CHECK(inRange(a.pad));
        CHECK(inRange(a.crickets));
        CHECK(inRange(a.hum));
      }
    }
  }
}

TEST_CASE("crickets sing at night outdoors and never indoors") {
  AmbienceLevels noon = ambienceFor(0.5f, false, "starter_town");
  AmbienceLevels night = ambienceFor(0.05f, false, "starter_town");
  CHECK(noon.crickets == 0.0f);
  CHECK(night.crickets > 0.4f);
  CHECK(ambienceFor(0.05f, true, "starter_town").crickets == 0.0f);
}

TEST_CASE("isNight matches the rendering band used by the renderer") {
  CHECK(isNight(0.0f));
  CHECK(isNight(0.24f));
  CHECK_FALSE(isNight(0.3f));
  CHECK_FALSE(isNight(0.5f));
  CHECK(isNight(0.9f));
}

TEST_CASE("interiors replace wind with room hum") {
  AmbienceLevels outside = ambienceFor(0.5f, false, "starter_town");
  AmbienceLevels inside = ambienceFor(0.5f, true, "starter_town");
  CHECK(inside.hum > outside.hum);
  CHECK(inside.wind < outside.wind);
}

TEST_CASE("the data center hums louder than the cafe") {
  AmbienceLevels systems = ambienceFor(0.5f, true, "systems_district");
  AmbienceLevels town = ambienceFor(0.5f, true, "starter_town");
  CHECK(systems.hum > town.hum);
}

TEST_CASE("wind is stronger by day than by night") {
  AmbienceLevels noon = ambienceFor(0.5f, false, "starter_town");
  AmbienceLevels night = ambienceFor(0.05f, false, "starter_town");
  CHECK(noon.wind > night.wind);
  CHECK(night.pad >= noon.pad);
}
