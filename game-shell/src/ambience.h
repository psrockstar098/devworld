#pragma once
// ambience.h — pure environmental-audio mix levels.
//
// The audio callback lives in audio.cpp (SDL). This header holds the part worth
// testing: how wind, pad, crickets and interior hum follow the day cycle and
// the kind of place you are in. Levels are 0..1 linear gains; the mixer scales
// them to safe amplitudes.

#include <string>

namespace dw {

struct AmbienceLevels {
  float wind = 0.0f;      // filtered noise: outdoors, stronger in the day
  float pad = 0.0f;       // low tonal bed: always present, night-heavy
  float crickets = 0.0f;  // pulsed high chirps: night outdoors only
  float hum = 0.0f;       // 50/100 Hz room tone: interiors and systems
};

// dayT: 0 = midnight, 0.25 = 6:00, 0.5 = noon, 0.75 = 18:00.
AmbienceLevels ambienceFor(float dayT, bool interior, const std::string& cityId);

// True during the "night" band used by rendering and crickets.
bool isNight(float dayT);

}  // namespace dw
