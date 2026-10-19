// tests for src/settings.h — accessibility prefs clamp and round-trip.
#include "doctest.h"
#include "settings.h"

using namespace dw;

TEST_CASE("defaults are in range and motion-safe") {
  AccessibilitySettings s;
  CHECK(s.uiScale == 100);
  CHECK(s.textScale == 100);
  CHECK_FALSE(s.reducedMotion);
  CHECK_FALSE(s.colorblindPalette);
  CHECK_FALSE(s.highContrast);
  CHECK_FALSE(s.screenShake);
  s.clamp();
  CHECK(s.uiScale == 100);
}

TEST_CASE("ui and text scale clamp to their supported ranges") {
  AccessibilitySettings s;
  s.uiScale = 5; s.textScale = 900;
  s.clamp();
  CHECK(s.uiScale == 80);
  CHECK(s.textScale == 150);

  s.uiScale = 999; s.textScale = 1;
  s.clamp();
  CHECK(s.uiScale == 130);
  CHECK(s.textScale == 80);

  s.uiScale = 110; s.textScale = 120;
  s.clamp();
  CHECK(s.uiScale == 110);
  CHECK(s.textScale == 120);
}

TEST_CASE("reduced motion forces screen shake off") {
  AccessibilitySettings s;
  s.reducedMotion = true;
  s.screenShake = true;
  s.clamp();
  CHECK_FALSE(s.screenShake);
}

TEST_CASE("parsing clamps hostile values from storage") {
  AccessibilitySettings s = AccessibilitySettings::parse(
      parseJson("{\"uiScale\":10,\"textScale\":10,\"reducedMotion\":true,\"screenShake\":true}"));
  CHECK(s.uiScale == 80);
  CHECK(s.textScale == 80);
  CHECK(s.reducedMotion);
  CHECK_FALSE(s.screenShake);
}

TEST_CASE("missing keys fall back to defaults") {
  AccessibilitySettings s = AccessibilitySettings::parse(parseJson("{}"));
  CHECK(s.uiScale == 100);
  CHECK(s.textScale == 100);
  CHECK_FALSE(s.colorblindPalette);
}

TEST_CASE("toJson round-trips through parse") {
  AccessibilitySettings a;
  a.uiScale = 125; a.textScale = 130;
  a.reducedMotion = true; a.colorblindPalette = true; a.highContrast = true;
  a.screenShake = false;
  AccessibilitySettings b = AccessibilitySettings::parse(parseJson(a.toJson()));
  CHECK(b.uiScale == a.uiScale);
  CHECK(b.textScale == a.textScale);
  CHECK(b.reducedMotion == a.reducedMotion);
  CHECK(b.colorblindPalette == a.colorblindPalette);
  CHECK(b.highContrast == a.highContrast);
  CHECK(b.screenShake == a.screenShake);
}

TEST_CASE("toJson always emits clamped values") {
  AccessibilitySettings a;
  a.uiScale = 1000;
  CHECK(a.toJson().find("\"uiScale\":130") != std::string::npos);
}
