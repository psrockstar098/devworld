// tests for src/critters.h — ambient wildlife wander logic.
#include "doctest.h"
#include "critters.h"

#include <set>

using namespace dw;

namespace {

// 800x600 open ground, with a blocked pond in the middle.
struct Walk {
  bool block(float x, float y) const {
    return x > 300 && x < 500 && y > 200 && y < 400;
  }
  bool operator()(float x, float y) const { return !block(x, y); }
};
Walk walk;

}  // namespace

TEST_CASE("spawning is deterministic per map and seed") {
  auto a = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  auto b = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); i++) {
    CHECK(a[i].kind == b[i].kind);
    CHECK(a[i].pos.x == b[i].pos.x);
    CHECK(a[i].pos.y == b[i].pos.y);
  }
  auto c = spawnCritters("plaza", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  CHECK_FALSE(c.empty());
}

TEST_CASE("every spawn lands on a walkable spot inside the map") {
  auto critters = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  REQUIRE_FALSE(critters.empty());
  for (const Critter& c : critters) {
    CHECK_FALSE(walk.block(c.pos.x, c.pos.y));
    bool insideX = c.pos.x > 0 && c.pos.x < 40 * 32;
    bool insideY = c.pos.y > 0 && c.pos.y < 30 * 32;
    CHECK(insideX);
    CHECK(insideY);
  }
}

TEST_CASE("day and night casts differ: butterflies by day, fireflies by night") {
  auto noon = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  auto midnight = spawnCritters("town", 40 * 32, 30 * 32, 0.05f, 7u, walk);
  bool noonHasButterfly = false, nightHasFirefly = false;
  for (const Critter& c : noon) if (c.kind == "butterfly") noonHasButterfly = true;
  for (const Critter& c : midnight) if (c.kind == "firefly") nightHasFirefly = true;
  CHECK(noonHasButterfly);
  CHECK(nightHasFirefly);
}

TEST_CASE("critterVisible hides butterflies at night and fireflies by day") {
  CHECK(critterVisible("butterfly", 0.5f));
  CHECK_FALSE(critterVisible("butterfly", 0.02f));
  CHECK(critterVisible("firefly", 0.02f));
  CHECK_FALSE(critterVisible("firefly", 0.5f));
  CHECK(critterVisible("cat", 0.5f));   // cats around the clock
  CHECK(critterVisible("cat", 0.02f));
}

TEST_CASE("critters wander near home and never settle on blocked ground") {
  auto critters = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  // Simulate ten minutes: plenty of target picks.
  for (int step = 0; step < 6000; step++)
    updateCritters(critters, 1.0f / 60.0f, 0.5f, walk);
  for (const Critter& c : critters) {
    CHECK_FALSE(walk.block(c.pos.x, c.pos.y));
    float dx = c.pos.x - c.home.x, dy = c.pos.y - c.home.y;
    float distSq = dx * dx + dy * dy;
    // Far less than the roam radius every frame would allow: home anchors them.
    CHECK(distSq < 300.0f * 300.0f);
  }
}

TEST_CASE("update with dt == 0 changes nothing") {
  auto critters = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  auto before = critters;
  updateCritters(critters, 0.0f, 0.5f, walk);
  REQUIRE(critters.size() == before.size());
  for (size_t i = 0; i < critters.size(); i++)
    CHECK(critters[i].pos.x == before[i].pos.x);
}

TEST_CASE("critters actually move: distance grows after a frame") {
  auto critters = spawnCritters("town", 40 * 32, 30 * 32, 0.5f, 7u, walk);
  // Force everyone walking by simulating a while.
  for (int step = 0; step < 600; step++)
    updateCritters(critters, 1.0f / 60.0f, 0.5f, walk);
  int moved = 0;
  for (const Critter& c : critters) {
    if (c.pos.x != c.home.x) moved++;
  }
  CHECK(moved > 0);  // the population is alive, not statues
}

TEST_CASE("fully blocked maps are safe: no crash, no stuck movers") {
  auto never = [](float, float) { return false; };
  auto critters = spawnCritters("pond", 20 * 32, 20 * 32, 0.5f, 3u, never);
  // With no walkable ground there is nowhere to live: the map stays empty.
  CHECK(critters.empty());
  updateCritters(critters, 1.0f, 0.5f, never);  // must not crash
}
