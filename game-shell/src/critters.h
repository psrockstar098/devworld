#pragma once
// critters.h — pure ambient-wildlife wander logic.
//
// Cats, birds, butterflies (day) and fireflies (night) that make the town
// streets feel lived-in. Purely decorative: never collides, never changes
// the sim, never interacts — like Decor props in world.h, but moving.
//
// The logic is split from rendering (game_loop.cpp) so it can be tested
// natively: spawnCritters is deterministic per (mapId, seed), and
// updateCritters takes a walkability callback so tests run without SDL.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "types.h"  // Vec2

namespace dw {

// "cat" | "bird" | "butterfly" | "firefly".
// cat: slow ground wanderer, day and night. bird: hoppy, day.
// butterfly: meanders, day only (roosts at night — not drawn).
// firefly: drifts near its home spot, night only (not drawn by day).
struct Critter {
  int id = 0;
  std::string kind;
  Vec2 pos{0, 0};        // px, center of the critter
  Vec2 home{0, 0};       // px it was spawned at; never strays far
  Vec2 target{0, 0};     // px it is walking toward
  float pauseLeft = 0;   // seconds of idle left before choosing a target
  float phase = 0;       // walk/flutter cycle phase
  float size = 1.0f;     // visual variety multiplier
  bool moving = false;
  bool facingLeft = false;
  uint32_t rng = 0;      // per-critter LCG state (deterministic wander)
};

// True when the critter kind is visible at this time of day.
// Butterflies and birds sleep at night; fireflies hide by day.
bool critterVisible(const std::string& kind, float dayT);

// Spawn the population for an exterior map. Deterministic: the same
// (mapId, seed) always yields the same critters, so a map's population is
// stable across visits. Positions are px; dayT picks the day/night cast.
// `walkable(px, py)` (optional) rejects spots inside walls, water and trees.
std::vector<Critter> spawnCritters(
    const std::string& mapId, float mapW, float mapH, float dayT, uint32_t seed,
    const std::function<bool(float, float)>& walkable = nullptr);

// Advance one frame. `walkable(px, py)` must be false for water/walls/trees —
// critters never enter them. With dt == 0 nothing changes (tests rely on it).
void updateCritters(std::vector<Critter>& critters, float dt, float dayT,
                    const std::function<bool(float, float)>& walkable);

}  // namespace dw
