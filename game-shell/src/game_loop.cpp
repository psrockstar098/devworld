// game.cpp (part 2) — main loop: update, render, input, interaction.
#include "game.h"
#include "bridge.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <emscripten.h>

namespace dw {

static uint32_t lastTick = 0;

static Color C(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

static bool jsonColor(const JVal& v, Color* out) {
  if (v.type != JVal::ARR || v.arr.size() != 3) return false;
  auto channel = [&](int i) {
    float n = (float)v.arr[i].num;
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    return (uint8_t)n;
  };
  *out = {channel(0), channel(1), channel(2), 255};
  return true;
}

std::string Game::appearanceJson() const {
  auto rgb = [](const Color& c) {
    return "[" + std::to_string((int)c.r) + "," + std::to_string((int)c.g) +
           "," + std::to_string((int)c.b) + "]";
  };
  return "{\"skin\":" + rgb(player_.skin) + ",\"shirt\":" + rgb(player_.shirt) +
         ",\"pants\":" + rgb(player_.pants) + ",\"hair\":" + rgb(player_.hair) +
         "}";
}

void Game::setAppearanceJson(const std::string& json) {
  JVal j;
  try {
    j = parseJson(json);
  } catch (...) {
    return;
  }
  Color c;
  if (jsonColor(j.at("skin"), &c)) player_.skin = c;
  if (jsonColor(j.at("shirt"), &c)) player_.shirt = c;
  if (jsonColor(j.at("pants"), &c)) player_.pants = c;
  if (jsonColor(j.at("hair"), &c)) player_.hair = c;
  saveNow();
}

void Game::upsertRemote(const JVal& j) {
  std::string id = j.s("id");
  if (id.empty()) return;
  auto found = remotePlayers_.find(id);
  RemotePlayer rp;
  bool existed = found != remotePlayers_.end();
  if (existed) rp = found->second;

  rp.id = id;
  rp.handle = j.s("handle");
  rp.displayName = j.s("displayName");
  if (rp.displayName.empty()) rp.displayName = rp.handle;
  std::string mapId = j.s("mapId", "town");
  if (!mapId.empty()) rp.mapId = mapId;

  const JVal& app = j.at("appearance");
  Color c;
  if (jsonColor(app.at("skin"), &c)) rp.skin = c;
  if (jsonColor(app.at("shirt"), &c)) rp.shirt = c;
  if (jsonColor(app.at("pants"), &c)) rp.pants = c;
  if (jsonColor(app.at("hair"), &c)) rp.hair = c;

  float x = (float)j.n("x", rp.pos.x);
  float y = (float)j.n("y", rp.pos.y);
  if (existed) {
    float dx = x - rp.pos.x, dy = y - rp.pos.y;
    if (dx * dx + dy * dy > 0.25f) {
      rp.facing = fabsf(dx) > fabsf(dy) ? (dx < 0 ? 1 : 2) : (dy < 0 ? 3 : 0);
      rp.moving = true;
      rp.movingT = 0.30f;
    }
  }
  rp.pos = {x, y};
  remotePlayers_[id] = rp;
}

void Game::worldEvent(const std::string& json) {
  JVal j;
  try {
    j = parseJson(json);
  } catch (...) {
    return;
  }
  std::string type = j.s("type");
  if (type == "world.move") {
    upsertRemote(j.at("player"));
  } else if (type == "world.leave") {
    remotePlayers_.erase(j.s("playerId"));
  } else if (type == "world.snapshot") {
    std::set<std::string> seen;
    const JVal& players = j.at("players");
    if (players.type == JVal::ARR) {
      for (const auto& p : players.arr) {
        std::string id = p.s("id");
        if (!id.empty()) seen.insert(id);
        upsertRemote(p);
      }
    }
    for (auto it = remotePlayers_.begin(); it != remotePlayers_.end();) {
      if (seen.find(it->first) == seen.end())
        it = remotePlayers_.erase(it);
      else
        ++it;
    }
  } else if (type == "player.updated") {
    const JVal& p = j.at("player");
    std::string id = p.s("id");
    auto it = remotePlayers_.find(id);
    if (it == remotePlayers_.end()) return;
    const JVal& app = p.at("appearance");
    Color c;
    if (jsonColor(app.at("skin"), &c)) it->second.skin = c;
    if (jsonColor(app.at("shirt"), &c)) it->second.shirt = c;
    if (jsonColor(app.at("pants"), &c)) it->second.pants = c;
    if (jsonColor(app.at("hair"), &c)) it->second.hair = c;
    std::string name = p.s("displayName");
    if (!name.empty()) it->second.displayName = name;
  }
}

void Game::publishPosition(bool force) {
  if (!hasSave_) return;
  int64_t now = now_ms();
  if (!force && now - lastPublishMs_ < 90) return;
  float dx = player_.pos.x - lastPublished_.x;
  float dy = player_.pos.y - lastPublished_.y;
  bool mapChanged = curMap_ != lastPublishedMap_;
  if (!force && !mapChanged && now - lastPublishMs_ < 4000 &&
      dx * dx + dy * dy < 1.0f)
    return;
  lastPublishMs_ = now;
  lastPublished_ = player_.pos;
  lastPublishedMap_ = curMap_;
  world_position(player_.pos.x, player_.pos.y, curMap_);
}

// Small integer UI primitives shared by the prompt and map banner.
static void uiRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
  if (w <= 0 || h <= 0 || c.a == 0) return;
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}
static void uiFrame(SDL_Renderer* r, int x, int y, int w, int h, Color c, int t = 2) {
  uiRect(r, x, y, w, t, c);
  uiRect(r, x, y + h - t, w, t, c);
  uiRect(r, x, y, t, h, c);
  uiRect(r, x + w - t, y, t, h, c);
}
static void uiText(SDL_Renderer* r, TTF_Font* font, const std::string& text,
                   int x, int y, Color c) {
  if (!font || text.empty()) return;
  SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), {c.r, c.g, c.b, c.a});
  if (!s) return;
  SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
  SDL_Rect dst{x, y, s->w, s->h};
  SDL_RenderCopy(r, t, nullptr, &dst);
  SDL_DestroyTexture(t);
  SDL_FreeSurface(s);
}

void Game::frame() {
  uint32_t now = SDL_GetTicks();
  float dt = lastTick ? (now - lastTick) / 1000.0f : 0.016f;
  lastTick = now;
  if (dt > 0.1f) dt = 0.1f;

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) return;
    if (e.type == SDL_KEYDOWN && !e.key.repeat) onKeyDown(e.key.keysym.sym);
    if (e.type == SDL_KEYUP) onKeyUp(e.key.keysym.sym);
  }

  if (!uiOpen_) update(dt);
  // Presence must outlive an open panel: standing still in a menu is not
  // leaving the world, so the heartbeat runs outside update().
  publishPosition();
  animT_ += dt;
  player_.playSeconds += dt;
  // Day cycle: full day ~ 10 minutes.
  dayT_ += dt / 600.0f;
  if (dayT_ >= 1.0f) dayT_ -= 1.0f;
  // Environment audio follows the same clock and the current map.
  audio_.setScene(currentCityId(), world_.map(curMap_).isInterior, dayT_);

  render();
}

void Game::onKeyDown(SDL_Keycode k) {
  held_[k] = true;
  if (k == keys_["close"]) {
    if (ide_is_open()) { ide_close(); sfx("close"); return; }
    if (uiOpen_) { ui_hide("all"); uiOpen_ = false; sfx("close"); return; }
    ui_show("pausemenu");
    uiOpen_ = true;
    return;
  }
  if (uiOpen_) return;  // DOM panels own the keyboard while open
  if (k == keys_["up"]) moveUp_ = true;
  else if (k == keys_["down"]) moveDown_ = true;
  else if (k == keys_["left"]) moveLeft_ = true;
  else if (k == keys_["right"]) moveRight_ = true;
  else if (k == keys_["interact"]) {
    Hit h;
    if (findInteract(h)) doInteract(h);
  } else if (k == keys_["map"]) { ui_show("worldmap", panelJson("worldmap")); uiOpen_ = true; sfx("open"); }
  else if (k == keys_["quests"]) { ui_show("quests", questJson()); uiOpen_ = true; sfx("open"); }
  else if (k == keys_["inventory"]) { ui_show("inventory", panelJson("inventory")); uiOpen_ = true; sfx("open"); }
  else if (k == keys_["profile"]) { ui_show("profile", panelJson("profile")); uiOpen_ = true; sfx("open"); }
  else if (k == keys_["notifications"]) { ui_show("notifications", notificationsJson()); uiOpen_ = true; sfx("open"); }
}

void Game::onKeyUp(SDL_Keycode k) {
  held_[k] = false;
  if (k == keys_["up"]) moveUp_ = false;
  else if (k == keys_["down"]) moveDown_ = false;
  else if (k == keys_["left"]) moveLeft_ = false;
  else if (k == keys_["right"]) moveRight_ = false;
}

bool Game::collides(float x, float y) const {
  const TileMap& m = world_.map(curMap_);
  // Player bbox ~ 20x24 centered at (x, y+... ) — check 4 corners.
  float hw = 9, hh = 11;
  int x0 = (int)((x - hw) / m.tile), x1 = (int)((x + hw) / m.tile);
  int y0 = (int)((y - hh) / m.tile), y1 = (int)((y + hh) / m.tile);
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++)
      if (m.solid(tx, ty)) return true;
  return false;
}

void Game::update(float dt) {
  float vx = 0, vy = 0;
  // Arrow keys always work as fallback (in addition to remappable WASD).
  if (moveUp_ || held_[SDLK_UP]) vy -= 1;
  if (moveDown_ || held_[SDLK_DOWN]) vy += 1;
  if (moveLeft_ || held_[SDLK_LEFT]) vx -= 1;
  if (moveRight_ || held_[SDLK_RIGHT]) vx += 1;
  if (vx != 0 && vy != 0) { vx *= 0.7071f; vy *= 0.7071f; }
  const float speed = 150.0f;  // px/s
  float nx = player_.pos.x + vx * speed * dt;
  float ny = player_.pos.y + vy * speed * dt;
  // Axis-separated collision (slide along walls).
  if (!collides(nx, player_.pos.y)) player_.pos.x = nx;
  if (!collides(player_.pos.x, ny)) player_.pos.y = ny;
  if (vx != 0 || vy != 0) {
    walkPhase_ += dt * 11.0f;
    if (fabsf(vx) > fabsf(vy)) facing_ = vx < 0 ? 1 : 2;
    else facing_ = vy < 0 ? 3 : 0;
  }
  if (bannerT_ > 0) bannerT_ = std::max(0.0f, bannerT_ - dt);
  // loc.regionId/instanceId are set by enterMap(); re-writing them here with
  // the map id would clobber the region the save persisted.
  updateCamera();
  updateNpcs(dt);
  updateCritterSim(dt);

  for (auto& entry : remotePlayers_) {
    RemotePlayer& rp = entry.second;
    if (rp.movingT > 0) {
      rp.movingT -= dt;
      if (rp.movingT <= 0) rp.moving = false;
    }
  }

  static float saveT = 0;
  saveT += dt;
  if (saveT > 30.0f) { saveT = 0; saveNow(); }
}

void Game::updateCamera() {
  const TileMap& m = world_.map(curMap_);
  float mw = m.w * m.tile, mh = m.h * m.tile;
  cam_.x = player_.pos.x - vw_ / 2;
  cam_.y = player_.pos.y - vh_ / 2;
  if (mw > vw_) { if (cam_.x < 0) cam_.x = 0; if (cam_.x > mw - vw_) cam_.x = mw - vw_; }
  else cam_.x = (mw - vw_) / 2;
  if (mh > vh_) { if (cam_.y < 0) cam_.y = 0; if (cam_.y > mh - vh_) cam_.y = mh - vh_; }
  else cam_.y = (mh - vh_) / 2;
}

Vec2 Game::npcPos(const NpcDef& n) const {
  float hour = dayT_ * 24.0f;
  const TileMap& m = world_.map(curMap_);
  for (auto& s : n.schedule) {
    bool in = (s.fromHour <= s.toHour) ? (hour >= s.fromHour && hour < s.toHour)
                                       : (hour >= s.fromHour || hour < s.toHour);
    if (in && s.mapId == curMap_)
      return {s.x * m.tile + m.tile / 2, s.y * m.tile + m.tile / 2};
  }
  return {-1000, -1000};  // off-map / not here right now
}

void Game::updateNpcs(float dt) {
  // Residents walk between schedule spots instead of teleporting, and while
  // posted at one they take the occasional short stroll around their anchor.
  // The schedule (see npcPos) is the destination; this eases toward it at a
  // stroll. Snap only on first sight after a map change, and for the
  // reduced-motion preference.
  const bool snap = player_.a11y.reducedMotion;
  const float speed = 60.0f;  // px/s — a laid-back stroll, half the player's
  const TileMap& m = world_.map(curMap_);
  auto walkable = [&](float x, float y) {
    return !m.solid((int)(x / m.tile), (int)(y / m.tile));
  };
  for (auto& n : npcs_) {
    Vec2 p = npcPos(n);
    if (p.x < -500) {  // off-map right now (asleep / at work elsewhere)
      npcLive_.erase(n.id);
      continue;
    }
    NpcLive& live = npcLive_[n.id];
    if (!live.seen) {
      live.pos = live.target = live.anchor = p;
      live.seen = true;
      live.moving = false;
      uint32_t h = 0;
      for (char c : n.id) h = h * 131 + (uint8_t)c;
      live.rng = h * 2654435761u + 12345u;
    } else if (p.x != live.anchor.x || p.y != live.anchor.y) {
      live.anchor = p;  // schedule slot changed: head for the new post
      live.target = p;
      live.moving = (live.pos.x != p.x || live.pos.y != p.y);
    }
    // Walk whenever we are not at the target: the schedule post or a stroll.
    Vec2 d = {live.target.x - live.pos.x, live.target.y - live.pos.y};
    float dist = hypotf(d.x, d.y);
    if (snap) {
      live.pos = live.target;
      live.moving = false;
    } else if (dist > 0.5f) {
      float step = speed * dt;
      if (dist <= step) {  // arrival: settle, then rest a while
        live.pos = live.target;
        live.moving = false;
        live.idleLeft = 6.0f + ((live.rng >> 4) / 16777216.0f) * 14.0f;
        live.rng = live.rng * 1664525u + 1013904223u;
      } else {
        live.pos.x += d.x / dist * step;
        live.pos.y += d.y / dist * step;
        live.moving = true;
        live.phase += dt * 11.0f;
        live.facing = (fabsf(d.x) > fabsf(d.y)) ? (d.x < 0 ? 1 : 2)
                                                : (d.y < 0 ? 3 : 0);
      }
    }
    if (live.moving) continue;
    // Posted and idle: now and then a short stroll near the anchor, back and
    // forth within a tile or two, like someone waiting out the shift.
    live.idleLeft -= dt;
    if (live.idleLeft > 0 || dt <= 0) continue;
    float ang = ((live.rng >> 8) / 16777216.0f) * 6.2831853f;
    float rad = (16.0f + ((live.rng >> 16) / 16777216.0f) * 28.0f) * m.tile / 32.0f;
    float x = live.anchor.x + cosf(ang) * rad;
    float y = live.anchor.y + sinf(ang) * rad;
    if (walkable(x, y)) {
      live.target = {x, y};
      live.moving = true;
    }
    live.idleLeft = 6.0f + ((live.rng >> 4) / 16777216.0f) * 14.0f;
    live.rng = live.rng * 1664525u + 1013904223u;
  }
}

Vec2 Game::npcLivePos(const NpcDef& n) {
  Vec2 p = npcPos(n);
  if (p.x < -500) return p;  // off-map
  auto it = npcLive_.find(n.id);
  if (it == npcLive_.end()) return p;  // not yet simulated: schedule spot
  return it->second.pos;
}

void Game::updateCritterSim(float dt) {
  const TileMap& m = world_.map(curMap_);
  if (m.isInterior) {  // wildlife lives outdoors only
    critters_.clear();
    crittersMap_.clear();
    return;
  }
  if (crittersMap_ != curMap_ || critters_.empty()) {
    float mapW = (float)m.w * m.tile, mapH = (float)m.h * m.tile;
    critters_ = spawnCritters(curMap_, mapW, mapH, dayT_, 7u, [&](float x, float y) {
      return !m.solid((int)(x / m.tile), (int)(y / m.tile));
    });
    crittersMap_ = curMap_;
  }
  if (player_.a11y.reducedMotion) return;  // critters rest
  updateCritters(critters_, dt, dayT_, [&](float x, float y) {
    return !m.solid((int)(x / m.tile), (int)(y / m.tile));
  });
}

void Game::renderCritters() {
  const TileMap& m = world_.map(curMap_);
  if (m.isInterior) return;
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);  // firefly glow
  for (const Critter& c : critters_) {
    if (!critterVisible(c.kind, dayT_)) continue;
    float sx = c.pos.x - cam_.x, sy = c.pos.y - cam_.y;
    if (sx < -40 || sx > vw_ + 40 || sy < -40 || sy > vh_ + 40) continue;
    float bob = c.moving ? sinf((float)c.phase) * 2.0f * c.size : 0;
    if (c.kind == "cat") {
      // body + head + ears + swaying tail
      float w = 13 * c.size, hgt = 8 * c.size;
      SDL_Rect body{(int)(sx - w / 2), (int)(sy - hgt / 2 - bob), (int)w, (int)hgt};
      SDL_SetRenderDrawColor(ren_, 96, 88, 84, 255);
      SDL_RenderFillRect(ren_, &body);
      SDL_Rect head{(int)(body.x + w - 5), (int)(body.y - 3), 6, 6};
      SDL_RenderFillRect(ren_, &head);
      SDL_SetRenderDrawColor(ren_, 70, 64, 60, 255);
      float tail = sinf((float)animT_ * 2.0f + c.id) * 3.0f;
      SDL_RenderDrawLine(ren_, body.x, body.y + 2, body.x - 6, (int)(body.y + tail));
      SDL_SetRenderDrawColor(ren_, 236, 240, 244, 255);
      SDL_RenderDrawPoint(ren_, head.x + 5, head.y + 2);  // eye
    } else if (c.kind == "bird") {
      // round body, head peck while pausing
      int r = (int)(3.5f * c.size);
      SDL_Rect body{(int)(sx - r), (int)(sy - r - bob), 2 * r, 2 * r};
      SDL_SetRenderDrawColor(ren_, 92, 74, 60, 255);
      SDL_RenderFillRect(ren_, &body);
      float peck = c.moving ? 0 : sinf((float)animT_ * 6 + c.id * 1.7f);
      SDL_Rect head{(int)(body.x - r + 1), (int)(body.y + (peck > 0.5f ? r : 0)), r, r};
      SDL_SetRenderDrawColor(ren_, 122, 100, 80, 255);
      SDL_RenderFillRect(ren_, &head);
    } else if (c.kind == "butterfly") {
      // two wings flapping around a bright dot
      float flap = sinf((float)(c.phase)) ;
      int wingW = (int)(4 * c.size * (0.4f + 0.6f * fabsf(flap)));
      SDL_SetRenderDrawColor(ren_, 232, 130, 70, 255);
      SDL_Rect wl{(int)(sx - wingW), (int)(sy - 3 - bob), wingW, 6};
      SDL_Rect wr{(int)(sx + 1), (int)(sy - 3 - bob), wingW, 6};
      SDL_RenderFillRect(ren_, &wl);
      SDL_RenderFillRect(ren_, &wr);
      SDL_SetRenderDrawColor(ren_, 46, 34, 30, 255);
      SDL_RenderDrawLine(ren_, (int)sx, (int)(sy - 4 - bob), (int)sx, (int)(sy + 1 - bob));
    } else {  // firefly: a blinking warm dot
      float blink = 0.5f + 0.5f * sinf((float)animT_ * 3.0f + c.id * 2.1f);
      Uint8 a = (Uint8)(120 + 135 * blink);
      SDL_SetRenderDrawColor(ren_, 255, 236, 150, a);
      int r = 1 + (int)(blink * 1.5f);
      SDL_Rect dot{(int)(sx - r), (int)(sy - r), 2 * r, 2 * r};
      SDL_RenderFillRect(ren_, &dot);
      SDL_SetRenderDrawColor(ren_, 255, 236, 150, (Uint8)(a / 4));
      SDL_Rect halo{(int)(sx - 3 * r), (int)(sy - 3 * r), 6 * r, 6 * r};
      SDL_RenderFillRect(ren_, &halo);
    }
  }
}

bool Game::findInteract(Hit& out) {
  const TileMap& m = world_.map(curMap_);
  float best = 1.6f * m.tile;
  bool found = false;
  auto consider = [&](const std::string& kind, const std::string& id,
                      const std::string& label, const std::string& target, Rect r) {
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    float d = hypotf(player_.pos.x - cx, player_.pos.y - cy);
    if (d < best) { best = d; found = true; out = {kind, id, label, target, {cx, cy}}; }
  };
  for (auto& it : m.interactables) {
    if (it.kind == "hatch" && !hatchRevealed(it)) continue;  // secrets stay secret
    consider(it.kind, it.id, it.label, it.target, it.rect);
  }
  for (auto& n : npcs_) {
    Vec2 p = npcLivePos(n);
    if (p.x < -500) continue;
    Rect r{p.x - 12, p.y - 12, 24, 24};
    consider("npc", n.id, n.name, "", r);
  }
  return found;
}

void Game::enterMap(const std::string& mapId, int tx, int ty) {
  if (!world_.has(mapId)) return;
  curMap_ = mapId;
  const TileMap& m = world_.map(mapId);
  if (tx < 0 || ty < 0) { tx = (int)m.spawn.x; ty = (int)m.spawn.y; }
  player_.pos = {(float)tx * m.tile + m.tile / 2, (float)ty * m.tile + m.tile / 2};
  // [PHASE2] the world-sim service assigns region/instance on entry; interiors
  // are instanced per player, exteriors are shared regions.
  player_.loc.regionId = m.regionId;
  player_.loc.instanceId = m.instanceType;
  // Resident schedules restart relative to this map; wildlife re-seeds for
  // the new map (deterministically, so a revisit shows the same crowd).
  npcLive_.clear();
  critters_.clear();
  crittersMap_.clear();
  bannerT_ = 2.6f;
  updateCamera();
  saveNow();
  publishPosition(true);
  sfx("open");
}

void Game::doInteract(const Hit& h) {
  sfx("interact");
  if (h.kind == "door") {
    if (!world_.has(h.target)) { notice("That door is under construction.", "error"); return; }
    const TileMap& tm = world_.map(h.target);
    int sx = 7, sy = 7;
    if (tm.isInterior) {
      // Stand just inside the door we came through.
      sx = tm.door.x;
      sy = tm.door.y - 1;
      if (sy < 1) sy = 1;
    } else {
      // Leaving a room: arrive at the door mat of the building we came from.
      sx = (int)tm.spawn.x;
      sy = (int)tm.spawn.y;
      for (const auto& b : tm.buildings) {
        if (b.target == curMap_) { sx = b.doorX; sy = b.doorY + 1; break; }
      }
    }
    enterMap(h.target, sx, sy);
    notice(tm.name, "info");
  } else if (h.kind == "npc") {
    ui_show("dialogue", npcDialogueJson(h.id, ""));
    uiOpen_ = true;
  } else if (h.kind == "computer") {
    // The machine's desktop opens in the page; the IDE is launched from it.
    uiOpen_ = true;
    computer_open();
    sfx("open");
  } else if (h.kind == "board") {
    ui_show("missionboard", missionsJson());
    uiOpen_ = true;
  } else if (h.kind == "shop") {
    ui_show("shop", panelJson("shop"));
    uiOpen_ = true;
  } else if (h.kind == "bank") {
    ui_show("bank", panelJson("bank"));
    uiOpen_ = true;
  } else if (h.kind == "bus") {
    ui_show("fasttravel", travelJson());
    uiOpen_ = true;
    sfx("open");
  } else if (h.kind == "mail") {
    // Mailbox: mission notices.
    int avail = 0;
    for (auto& kv : player_.missions)
      if (kv.second.state == MissionState::Available) avail++;
    notice(avail ? std::to_string(avail) + " mission(s) on the board. The town is hiring." : "No new notices. Check the mission board at the plaza.", "info");
  } else if (h.kind == "inspect") {
    ui_show("examine", examineJson(h.id));
    uiOpen_ = true;
  } else if (h.kind == "hatch") {
    if (!world_.has(h.target)) { notice("The hatch is sealed shut.", "error"); return; }
    const TileMap& tm = world_.map(h.target);
    int sx = 7, sy = 7;
    if (tm.isInterior) {
      sx = tm.door.x;
      sy = tm.door.y - 1;
      if (sy < 1) sy = 1;
    }
    enterMap(h.target, sx, sy);
    notice("You descend into " + tm.name + ".", "quest");
  }
}

void Game::computerAction(const std::string& action) {
  if (action == "board") {
    ui_hide("computer");
    ui_show("missionboard", missionsJson());
    uiOpen_ = true;
    return;
  }
  if (action == "portfolio") {
    ui_hide("computer");
    ui_show("portfolio", portfolioJson());
    uiOpen_ = true;
    return;
  }
  if (action == "mail") {
    ui_hide("computer");
    ui_show("notifications", notificationsJson());
    uiOpen_ = true;
    return;
  }
  if (action == "town") {
    ui_hide("computer");
    uiOpen_ = false;
    return;
  }
  // The desktop can launch any of the physical terminals the world already
  // understands. Keep this to real panels so a typo cannot open an empty UI.
  if (action.rfind("panel:", 0) == 0) {
    const std::string panel = action.substr(6);
    static const char* allowed[] = {
        "shop", "bank", "inventory", "skilltree", "worldmap", "settings",
        "progression", "home", "company", "profile", "portfolio",
        "notifications", "missionboard"};
    bool ok = false;
    for (const char* name : allowed)
      if (panel == name) ok = true;
    if (!ok) return;
    ui_hide("computer");
    ui_show(panel, panelJson(panel));
    uiOpen_ = true;
    return;
  }
  if (action != "ide") return;
  ui_hide("computer");
  ide_open();
  // If the player has an active mission, the overlay bar offers Submit.
  std::string bar = "{}";
  for (auto& kv : player_.missions) {
    if (kv.second.state == MissionState::Active) {
      const MissionDef& m = missions_[kv.first];
      bar = "{\"mission\":" + json_escape(m.id) + ",\"title\":" +
            json_escape(m.title) + "}";
      break;
    }
  }
  // A practice run has no Active slot; offer Submit for it too.
  if (bar == "{}" && !practiceMission_.empty()) {
    auto mi = missions_.find(practiceMission_);
    if (mi != missions_.end())
      bar = "{\"mission\":" + json_escape(mi->first) + ",\"title\":" +
            json_escape(mi->second.title) + ",\"practice\":true}";
  }
  ui_show("idebar", bar);
  sfx("open");
}

static Color npcHairColor(const std::string& id) {
  static const Color palette[6] = {
      {44, 34, 28}, {88, 58, 40}, {168, 128, 82},
      {28, 30, 36}, {150, 150, 150}, {132, 62, 66}};
  uint32_t h = 0;
  for (char c : id) h = h * 131 + (uint8_t)c;
  return palette[h % 6];
}

static Color npcPantColor(const Color& shirt) {
  return {uint8_t(std::min(255, (shirt.r + 66) / 2)),
          uint8_t(std::min(255, (shirt.g + 74) / 2)),
          uint8_t(std::min(255, (shirt.b + 88) / 2))};
}

void Game::render() {
  const TileMap& m = world_.map(curMap_);
  SDL_SetRenderDrawColor(ren_, 12, 16, 24, 255);
  SDL_RenderClear(ren_);

  // Undiscovered secrets stay off the screen entirely.
  std::vector<std::string> hidden;
  for (const auto& it : m.interactables)
    if (it.kind == "hatch" && !hatchRevealed(it)) hidden.push_back(it.id);

  // Character sprites scale with the map's tile size.
  g_charScale = m.isInterior ? 1.5f : 1.0f;

  world_.render(ren_, font_, curMap_, cam_, vw_, vh_, dayT_, animT_, hidden);

  // Residents (walking their schedule routes). They face the player when
  // close and idle, otherwise face where they are headed.
  for (auto& n : npcs_) {
    Vec2 p = npcLivePos(n);
    if (p.x < -500) continue;
    float sx = p.x - cam_.x, sy = p.y - cam_.y;
    if (sx < -80 || sx > vw_ + 80 || sy < -100 || sy > vh_ + 100) continue;
    const NpcLive* live = nullptr;
    auto it = npcLive_.find(n.id);
    if (it != npcLive_.end()) live = &it->second;
    bool moving = live ? live->moving : false;
    float phase = live ? (moving ? live->phase
                                 : (float)animT_ * 1.4f + (float)(n.id.size() * 3))
                       : (float)animT_ * 1.4f;
    int facing;
    if (moving) {
      facing = live->facing;
    } else {
      float dx = player_.pos.x - p.x, dy = player_.pos.y - p.y;
      facing = 0;
      if (fabsf(dx) + fabsf(dy) < 150)
        facing = (fabsf(dx) > fabsf(dy)) ? (dx < 0 ? 1 : 2) : (dy < 0 ? 3 : 0);
    }
    world_.drawCharacter(ren_, font_, sx, sy, n.skin, n.shirt, npcPantColor(n.shirt),
                         npcHairColor(n.id), n.name, facing, phase, moving, false);
  }

  // Other real accounts sharing this map. Interiors stay private; the town and
  // other exteriors are one world for everyone.
  if (!m.isInterior) {
    for (auto& entry : remotePlayers_) {
      RemotePlayer& rp = entry.second;
      if (rp.mapId != curMap_) continue;
      float sx = rp.pos.x - cam_.x, sy = rp.pos.y - cam_.y;
      if (sx < -100 || sx > vw_ + 100 || sy < -140 || sy > vh_ + 140) continue;
      int facing = rp.facing;
      float dx = player_.pos.x - rp.pos.x, dy = player_.pos.y - rp.pos.y;
      if (!rp.moving && fabsf(dx) + fabsf(dy) < 140)
        facing = (fabsf(dx) > fabsf(dy)) ? (dx < 0 ? 1 : 2) : (dy < 0 ? 3 : 0);
      float phase = (float)animT_ * 11.0f + (float)(rp.pos.x * 0.01f);
      world_.drawCharacter(ren_, font_, sx, sy, rp.skin, rp.shirt, rp.pants,
                           rp.hair, rp.displayName, facing, phase, rp.moving,
                           false);
    }
  }

  // Player.
  {
    float sx = player_.pos.x - cam_.x, sy = player_.pos.y - cam_.y;
    bool moving = moveUp_ || moveDown_ || moveLeft_ || moveRight_;
    if (player_.a11y.reducedMotion) moving = false;
    world_.drawCharacter(ren_, font_, sx, sy, player_.skin, player_.shirt,
                         player_.pants, player_.hair, player_.displayName,
                         facing_, walkPhase_, moving, true);
  }

  // Atmosphere and lamp light on top of everything so the whole scene grades.
  // Ambient wildlife between the characters and the light pass.
  renderCritters();

  world_.renderLighting(ren_, curMap_, cam_, vw_, vh_, dayT_, animT_);

  // Map-name banner when entering a new place.
  if (bannerT_ > 0 && !uiOpen_) {
    const CityDef* city = currentCity();
    std::string title = m.name.empty() ? curMap_ : m.name;
    std::string sub = city ? city->name : "Starter Town";
    int tw = 0, th = 0;
    TTF_SizeUTF8(font_, title.c_str(), &tw, &th);
    int sw = 0, sh = 0;
    TTF_SizeUTF8(font_, sub.c_str(), &sw, &sh);
    int cw = std::max(tw, sw) + 56;
    int cx = (vw_ - cw) / 2, cy = 84;
    float a = std::min(1.0f, std::min(bannerT_ / 0.45f, (2.6f - bannerT_) / 0.35f));
    if (a < 0) a = 0;
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    uiRect(ren_, cx + 4, cy + 4, cw, 54, C(6, 10, 14, (uint8_t)(150 * a)));
    uiRect(ren_, cx, cy, cw, 54, C(15, 21, 28, (uint8_t)(230 * a)));
    uiFrame(ren_, cx, cy, cw, 54, C(52, 190, 174, (uint8_t)(210 * a)), 2);
    uiRect(ren_, cx + 16, cy + 10, 3, 34, C(240, 186, 96, (uint8_t)(230 * a)));
    uiText(ren_, font_, title, cx + 30, cy + 8, {244, 240, 226, (uint8_t)(255 * a)});
    uiText(ren_, font_, sub, cx + 30, cy + 28, {150, 166, 162, (uint8_t)(235 * a)});
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
  }

  // Interaction prompt.
  if (!uiOpen_) {
    drawMinimap();
    Hit h;
    if (findInteract(h)) drawPrompt(h.label);
  }

  SDL_RenderPresent(ren_);
}

void Game::drawMinimap() {
  const TileMap& m = world_.map(curMap_);
  if (m.w == 0 || m.h == 0) return;
  const int mw = 176, mh = 130;
  const int mx = vw_ - mw - 16, my = 88;
  const int pad = 8;
  float sc = std::min((float)(mw - pad * 2) / m.w, (float)(mh - pad * 2) / m.h);

  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  uiRect(ren_, mx + 4, my + 4, mw, mh, C(5, 9, 13, 150));
  uiRect(ren_, mx, my, mw, mh, C(12, 18, 24, 220));
  uiFrame(ren_, mx, my, mw, mh, C(58, 88, 92, 230), 2);
  uiFrame(ren_, mx + 2, my + 2, mw - 4, mh - 4, C(24, 34, 40, 200), 1);

  // Map floor.
  auto cell = [&](int tx, int ty, Color c, int grow = 0) {
    int x = mx + pad + (int)(tx * sc);
    int y = my + pad + (int)(ty * sc);
    int w = std::max(1, (int)std::ceil(sc)) + grow;
    int h = std::max(1, (int)std::ceil(sc)) + grow;
    uiRect(ren_, x, y, w, h, c);
  };
  Color grass = m.isInterior ? C(78, 66, 58) : C(62, 104, 72);
  for (int ty = 0; ty < m.h; ty++) {
    for (int tx = 0; tx < m.w; tx++) {
      Tile t = m.at(tx, ty);
      Color c = grass;
      if (t == Tile::Path) c = C(166, 146, 114);
      else if (t == Tile::Plaza) c = C(158, 152, 138);
      else if (t == Tile::Water) c = C(54, 112, 168);
      else if (t == Tile::Tree) c = C(38, 82, 48);
      else if (t == Tile::Wall) c = m.isInterior ? C(110, 100, 88) : C(52, 62, 78);
      cell(tx, ty, c);
    }
  }
  // Buildings stand proud.
  for (const auto& b : m.buildings)
    cell((int)b.rect.x, (int)b.rect.y, C(112, 128, 150, 230),
         (int)b.rect.w > 1 ? (int)(b.rect.w * sc) - 1 : 0);
  // Points of interest.
  for (const auto& it : m.interactables) {
    if (it.kind == "door" || it.kind == "computer") continue;
    if (it.kind == "hatch" && !hatchRevealed(it)) continue;
    int tx = (int)(it.rect.x / m.tile), ty = (int)(it.rect.y / m.tile);
    Color c = it.kind == "bus" ? Color{250, 196, 112, 255}
            : it.kind == "board" ? Color{70, 214, 192, 255}
            : it.kind == "bank" ? Color{152, 214, 158, 255}
            : it.kind == "hatch" ? Color{206, 150, 250, 255}
            : it.kind == "inspect" ? Color{202, 198, 188, 255}
            : Color{224, 216, 196, 255};
    cell(tx, ty, c, 1);
  }

  // NPC dots (live positions, so residents show as walking on the minimap).
  for (const auto& n : npcs_) {
    Vec2 p = npcLivePos(n);
    if (p.x < -500) continue;
    int tx = (int)(p.x / m.tile), ty = (int)(p.y / m.tile);
    cell(tx, ty, C(240, 236, 220, 240), 1);
  }

  // Player marker with a facing nub.
  int ptx = (int)(player_.pos.x / m.tile), pty = (int)(player_.pos.y / m.tile);
  float blink = player_.a11y.reducedMotion ? 1.0f : 0.65f + 0.35f * sinf((float)animT_ * 4);
  int px = mx + pad + (int)(ptx * sc), py = my + pad + (int)(pty * sc);
  uiRect(ren_, px - 1, py - 1, std::max(3, (int)sc) + 2, std::max(3, (int)sc) + 2,
      C(70, 238, 212, (uint8_t)(170 + 80 * blink)));
  if (sc >= 3) {
    int d = std::max(2, (int)sc);
    if (facing_ == 0) uiRect(ren_, px + d / 2, py + d, 2, 3, C(240, 255, 250));
    else if (facing_ == 3) uiRect(ren_, px + d / 2, py - 3, 2, 3, C(240, 255, 250));
    else if (facing_ == 1) uiRect(ren_, px - 3, py + d / 2, 3, 2, C(240, 255, 250));
    else if (facing_ == 2) uiRect(ren_, px + d, py + d / 2, 3, 2, C(240, 255, 250));
  }

  // Compass.
  uiText(ren_, font_, "N", mx + mw - 16, my + 5, {190, 204, 200, 230});
  uiRect(ren_, mx + mw - 14, my + 22, 2, 8, C(190, 204, 200, 200));
  uiRect(ren_, mx + mw - 16, my + 30, 4, 2, C(190, 204, 200, 200));

  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
}

void Game::drawPrompt(const std::string& text) {
  if (!font_) return;
  // The keycap shows the *current* interact keybind, not a hardcoded E, so
  // the prompt stays truthful after a rebind.
  std::string cap = SDL_GetKeyName(keys_["interact"]);
  int cw = 0, ch = 0;
  TTF_SizeUTF8(font_, cap.c_str(), &cw, &ch);
  int bw = cw < 14 ? 24 : cw + 10;  // keycap box: one letter fits as before
  int tw = 0, th = 0;
  TTF_SizeUTF8(font_, text.c_str(), &tw, &th);
  int w = bw + 20 + tw + 14, h = 40;
  int x = (vw_ - w) / 2, y = vh_ - h - 36;
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  uiRect(ren_, x + 4, y + 4, w, h, C(5, 8, 12, 170));
  uiRect(ren_, x, y, w, h, C(14, 20, 26, 236));
  uiFrame(ren_, x, y, w, h, C(56, 190, 172, 235), 2);
  // Keycap.
  uiRect(ren_, x + 9, y + 8, bw, 24, C(26, 40, 42, 255));
  uiFrame(ren_, x + 9, y + 8, bw, 24, C(84, 230, 204, 255), 2);
  uiText(ren_, font_, cap.c_str(), x + 9 + (bw - cw) / 2, y + 20 - ch / 2,
         {236, 250, 244, 255});
  uiText(ren_, font_, text.c_str(), x + bw + 20, y + (h - th) / 2,
         {238, 242, 232, 255});
  // Little corner ticks: readable retro framing without noisy decoration.
  uiRect(ren_, x + 1, y + 1, 6, 2, C(240, 190, 96, 220));
  uiRect(ren_, x + w - 7, y + 1, 6, 2, C(240, 190, 96, 220));
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
}

void Game::shutdown() {
  saveNow();
  audio_.close();
  if (font_) TTF_CloseFont(font_);
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) SDL_DestroyWindow(win_);
  TTF_Quit();
  SDL_Quit();
}

}  // namespace dw
