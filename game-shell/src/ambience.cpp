#include "ambience.h"

#include <algorithm>

namespace dw {

static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

bool isNight(float dayT) { return dayT < 0.25f || dayT > 0.8f; }

AmbienceLevels ambienceFor(float dayT, bool interior, const std::string& cityId) {
  AmbienceLevels a;

  // Daylight curve: brightest around noon, zero at deep night.
  float day = 1.0f;
  if (dayT < 0.25f) day = 0.0f;
  else if (dayT < 0.35f) day = (dayT - 0.25f) / 0.1f;
  else if (dayT > 0.8f) day = 0.0f;
  else if (dayT > 0.7f) day = (0.8f - dayT) / 0.1f;

  float night = 1.0f - day;

  a.wind = 0.10f + 0.30f * day;
  a.pad = 0.12f + 0.22f * night;
  a.crickets = interior ? 0.0f : 0.55f * night;
  a.hum = 0.0f;

  if (interior) {
    a.wind *= 0.25f;
    a.pad *= 0.75f;
    a.hum = 0.32f;
  }

  if (cityId == "systems_district") {
    a.hum = std::max(a.hum, interior ? 0.42f : 0.18f);
    a.pad += 0.04f;
  } else if (cityId == "startup_city") {
    a.pad += 0.03f;
  } else if (cityId == "innovation_capital") {
    a.pad += 0.06f;
    a.wind *= 0.8f;
  }

  a.wind = clamp01(a.wind);
  a.pad = clamp01(a.pad);
  a.crickets = clamp01(a.crickets);
  a.hum = clamp01(a.hum);
  return a;
}

}  // namespace dw
