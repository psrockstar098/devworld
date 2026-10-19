// game.cpp (part 2) — main loop: update, render, input, interaction.
#include "game.h"
#include "bridge.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <emscripten.h>

namespace dw {

static uint32_t lastTick = 0;

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
  player_.loc.regionId = curMap_;
  updateCamera();
  updateNpcs();

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

void Game::updateNpcs() {
  // Phase 1: NPCs snap to schedule positions (no pathfinding).
  // [PHASE2] world-sim service drives positions; client interpolates.
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
    Vec2 p = npcPos(n);
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
  updateCamera();
  saveNow();
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
    ide_open();
    // If the player has an active mission, the overlay bar offers Submit.
    std::string bar = "{}";
    for (auto& kv : player_.missions) {
      if (kv.second.state == MissionState::Active) {
        const MissionDef& m = missions_[kv.first];
        bar = "{\"mission\":" + json_escape(m.id) + ",\"title\":" + json_escape(m.title) + "}";
        break;
      }
    }
    ui_show("idebar", bar);
    sfx("open");
    // uiOpen stays false: the world is behind the overlay; Esc closes.
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

void Game::render() {
  SDL_SetRenderDrawColor(ren_, 12, 16, 24, 255);
  SDL_RenderClear(ren_);
  world_.render(ren_, font_, curMap_, cam_, vw_, vh_, dayT_, animT_);

  const TileMap& m = world_.map(curMap_);
  // NPCs (scheduled).
  for (auto& n : npcs_) {
    Vec2 p = npcPos(n);
    if (p.x < -500) continue;
    float sx = p.x - cam_.x, sy = p.y - cam_.y;
    // (world::drawPlayerNpc is private; replicate simple draw here via public helper)
    // We draw via a small local: reuse world renderer's text for names.
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 60);
    SDL_Rect sh{(int)(sx - 9), (int)(sy + 22), 18, 6};
    SDL_RenderFillRect(ren_, &sh);
    SDL_SetRenderDrawColor(ren_, n.shirt.r, n.shirt.g, n.shirt.b, 255);
    SDL_Rect torso{(int)(sx - 8), (int)(sy - 4), 16, 15};
    SDL_RenderFillRect(ren_, &torso);
    SDL_SetRenderDrawColor(ren_, n.skin.r, n.skin.g, n.skin.b, 255);
    for (int w = 0; w < 16; w++)
      for (int hh = 0; hh < 16; hh++) {
        float dx = 8 - w, dy = 8 - hh;
        if (dx * dx + dy * dy <= 64) SDL_RenderDrawPoint(ren_, (int)(sx - 8 + w), (int)(sy - 18 + hh));
      }
    // name pill
    if (font_) {
      SDL_Surface* s = TTF_RenderUTF8_Blended(font_, n.name.c_str(), {240, 240, 245, 255});
      if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(ren_, s);
        SDL_SetRenderDrawColor(ren_, 10, 14, 20, 170);
        SDL_Rect bg{(int)(sx - s->w / 2 - 4), (int)(sy - 38), s->w + 8, s->h + 4};
        SDL_RenderFillRect(ren_, &bg);
        SDL_Rect r{(int)(sx - s->w / 2), (int)(sy - 36), s->w, s->h};
        SDL_RenderCopy(ren_, t, nullptr, &r);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
      }
    }
  }

  // Player.
  {
    float sx = player_.pos.x - cam_.x, sy = player_.pos.y - cam_.y;
    bool moving = moveUp_ || moveDown_ || moveLeft_ || moveRight_;
    float bob = (moving && !player_.a11y.reducedMotion) ? (float)animT_ : 0;
    float by = sy + (moving && !player_.a11y.reducedMotion ? -2.0f * fabsf(sinf(bob * 8.0f)) : 0);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 60);
    SDL_Rect sh{(int)(sx - 9), (int)(sy + 22), 18, 6};
    SDL_RenderFillRect(ren_, &sh);
    auto& P = player_;
    SDL_SetRenderDrawColor(ren_, P.pants.r, P.pants.g, P.pants.b, 255);
    SDL_Rect l1{(int)(sx - 7), (int)(by + 10), 6, 12}, l2{(int)(sx + 1), (int)(by + 10), 6, 12};
    SDL_RenderFillRect(ren_, &l1); SDL_RenderFillRect(ren_, &l2);
    SDL_SetRenderDrawColor(ren_, P.shirt.r, P.shirt.g, P.shirt.b, 255);
    SDL_Rect torso{(int)(sx - 8), (int)(by - 4), 16, 15};
    SDL_RenderFillRect(ren_, &torso);
    SDL_SetRenderDrawColor(ren_, P.skin.r, P.skin.g, P.skin.b, 255);
    for (int w = 0; w < 16; w++)
      for (int hh = 0; hh < 16; hh++) {
        float dx = 8 - w, dy = 8 - hh;
        if (dx * dx + dy * dy <= 64) SDL_RenderDrawPoint(ren_, (int)(sx - 8 + w), (int)(by - 18 + hh));
      }
    SDL_SetRenderDrawColor(ren_, P.hair.r, P.hair.g, P.hair.b, 255);
    SDL_Rect hair{(int)(sx - 8), (int)(by - 19), 16, 5};
    SDL_RenderFillRect(ren_, &hair);
  }

  // Interaction prompt.
  if (!uiOpen_) {
    drawMinimap();
    Hit h;
    if (findInteract(h)) drawPrompt("[E] " + h.label);
  }

  SDL_RenderPresent(ren_);
}

void Game::drawMinimap() {
  const TileMap& m = world_.map(curMap_);
  if (m.w == 0 || m.h == 0) return;
  const int mw = 150, mh = 108;
  const int mx = vw_ - mw - 14, my = 14;
  float sc = std::min((float)(mw - 8) / (m.w * m.tile), (float)(mh - 8) / (m.h * m.tile));

  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren_, 8, 12, 18, 190);
  SDL_Rect bg{mx, my, mw, mh};
  SDL_RenderFillRect(ren_, &bg);
  SDL_SetRenderDrawColor(ren_, 45, 212, 191, 200);
  SDL_RenderDrawRect(ren_, &bg);

  auto mark = [&](float wx, float wy, int gw, int gh, Color c) {
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_Rect r{mx + 4 + (int)(wx * sc), my + 4 + (int)(wy * sc),
               std::max(2, gw), std::max(2, gh)};
    SDL_RenderFillRect(ren_, &r);
  };

  for (const auto& b : m.buildings)
    mark(b.rect.x * m.tile, b.rect.y * m.tile,
         (int)(b.rect.w * m.tile * sc), (int)(b.rect.h * m.tile * sc), {150, 170, 200, 220});

  for (const auto& it : m.interactables) {
    if (it.kind == "door" || it.kind == "computer") continue;
    if (it.kind == "hatch" && !hatchRevealed(it)) continue;
    Color c = it.kind == "bus" ? Color{255, 210, 130, 255}
            : it.kind == "board" ? Color{45, 212, 191, 255}
            : it.kind == "bank" ? Color{160, 220, 160, 255}
            : it.kind == "hatch" ? Color{220, 160, 255, 255}
            : Color{210, 210, 220, 255};
    mark(it.rect.x, it.rect.y, 3, 3, c);
  }

  for (const auto& n : npcs_) {
    Vec2 p = npcPos(n);
    if (p.x < -500) continue;
    // npcPos is px already; convert back to map space for the scaled mark.
    mark(p.x, p.y, 3, 3, {240, 240, 245, 230});
  }

  float blink = player_.a11y.reducedMotion ? 1.0f : 0.6f + 0.4f * sinf((float)animT_ * 4);
  mark(player_.pos.x, player_.pos.y, 4, 4, {45, 212, 191, (uint8_t)(160 + 95 * blink)});
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
}

void Game::drawPrompt(const std::string& text) {
  if (!font_) return;
  SDL_Surface* s = TTF_RenderUTF8_Blended(font_, text.c_str(), {235, 245, 245, 255});
  if (!s) return;
  SDL_Texture* t = SDL_CreateTextureFromSurface(ren_, s);
  int w = s->w + 20, h = s->h + 12;
  int x = (vw_ - w) / 2, y = vh_ - h - 76;
  SDL_SetRenderDrawColor(ren_, 12, 18, 26, 220);
  SDL_Rect bg{x, y, w, h};
  SDL_RenderFillRect(ren_, &bg);
  SDL_SetRenderDrawColor(ren_, 45, 212, 191, 255);
  SDL_RenderDrawRect(ren_, &bg);
  SDL_Rect r{x + 10, y + 6, s->w, s->h};
  SDL_RenderCopy(ren_, t, nullptr, &r);
  SDL_DestroyTexture(t);
  SDL_FreeSurface(s);
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
