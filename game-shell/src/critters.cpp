#include "critters.h"

#include <cmath>

#include "ambience.h"  // isNight: one day/night band for the whole shell

namespace dw {
namespace {

// Small deterministic LCG (same shape of determinism as npcHairColor's id
// hash): wander paths are reproducible in tests and stable per map visit.
uint32_t nextRand(uint32_t* s) {
  *s = *s * 1664525u + 1013904223u;
  return *s;
}

float randRange(uint32_t* s, float lo, float hi) {
  return lo + (hi - lo) * (float)(nextRand(s) >> 8) / 16777216.0f;
}

uint32_t hashStr(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h * 16777619u) ^ (uint8_t)c;
  return h;
}

struct KindTraits {
  float speed;         // px/s
  float roamRadius;    // how far from home it strays
  float pauseMin, pauseMax;
};

KindTraits traitsFor(const std::string& kind) {
  if (kind == "bird") return {42.0f, 120.0f, 0.4f, 2.2f};
  if (kind == "butterfly") return {34.0f, 90.0f, 0.8f, 2.6f};
  if (kind == "firefly") return {14.0f, 60.0f, 0.6f, 3.0f};
  return {26.0f, 110.0f, 1.5f, 5.0f};  // cat: lazy
}

// The night band comes from ambience.cpp's isNight — one source of truth for
// when the lights change, the crickets sing and the fireflies come out.
bool nightBand(float dayT) { return isNight(dayT); }

std::string pickKind(uint32_t* s, float dayT) {
  bool night = nightBand(dayT);
  float r = randRange(s, 0.0f, 1.0f);
  if (night) {
    // Nights belong to cats and fireflies.
    return (r < 0.4f) ? "cat" : "firefly";
  }
  if (r < 0.35f) return "cat";
  if (r < 0.65f) return "bird";
  return "butterfly";
}

}  // namespace

bool critterVisible(const std::string& kind, float dayT) {
  bool night = nightBand(dayT);
  if (kind == "firefly") return night;
  if (kind == "butterfly") return !night;
  return true;  // cats and birds around the clock
}

std::vector<Critter> spawnCritters(
    const std::string& mapId, float mapW, float mapH, float dayT, uint32_t seed,
    const std::function<bool(float, float)>& walkable) {
  std::vector<Critter> out;
  uint32_t rng = hashStr(mapId) * 31u + seed;
  // Scales with map size but stays sparse: ~1 critter per 40x40 tiles of
  // ground, capped so large cities do not turn into a petting zoo.
  int count = (int)std::sqrt((mapW / 32.0f) * (mapH / 32.0f) / 40.0f);
  if (count < 3) count = 3;
  if (count > 10) count = 10;

  for (int i = 0; i < count; i++) {
    std::string kind = pickKind(&rng, dayT);
    for (int attempt = 0; attempt < 8; attempt++) {
      float x = randRange(&rng, 48.0f, mapW - 48.0f);
      float y = randRange(&rng, 48.0f, mapH - 48.0f);
      if (walkable && !walkable(x, y)) continue;
      Critter c;
      c.id = i;
      c.kind = kind;
      c.pos = c.home = c.target = {x, y};
      c.pauseLeft = randRange(&rng, 0.2f, 3.0f);
      c.size = randRange(&rng, 0.85f, 1.15f);
      c.rng = rng ^ (uint32_t)(i * 7919u);
      out.push_back(c);
      break;
    }
  }
  return out;
}

void updateCritters(std::vector<Critter>& critters, float dt, float dayT,
                    const std::function<bool(float, float)>& walkable) {
  if (dt <= 0) return;
  for (Critter& c : critters) {
    KindTraits t = traitsFor(c.kind);
    if (c.moving) {
      Vec2 d = {c.target.x - c.pos.x, c.target.y - c.pos.y};
      float dist = std::hypotf(d.x, d.y);
      float step = t.speed * c.size * dt;
      if (dist <= step) {
        c.pos = c.target;
        c.moving = false;
        c.pauseLeft = randRange(&c.rng, t.pauseMin, t.pauseMax);
      } else {
        c.pos.x += d.x / dist * step;
        c.pos.y += d.y / dist * step;
        if (std::fabs(d.x) > 0.5f * std::fabs(d.y)) c.facingLeft = d.x < 0;
        c.phase += dt * (c.kind == "butterfly" ? 18.0f : 9.0f);
      }
      continue;
    }
    c.pauseLeft -= dt;
    if (c.pauseLeft > 0) continue;
    // Pick a new spot: near home, walkable, inside bounds.
    for (int attempt = 0; attempt < 8; attempt++) {
      float ang = randRange(&c.rng, 0.0f, 6.2831853f);
      float rad = randRange(&c.rng, 24.0f, t.roamRadius);
      float x = c.home.x + std::cos(ang) * rad;
      float y = c.home.y + std::sin(ang) * rad;
      if (x < 16 || y < 16) continue;
      if (walkable && !walkable(x, y)) continue;
      c.target = {x, y};
      c.moving = true;
      break;
    }
    if (!c.moving) c.pauseLeft = 1.0f;  // hemmed in: try again shortly
  }
}

}  // namespace dw
