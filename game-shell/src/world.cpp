// world.cpp — map loading from JSON + cozy 2D rendering.
// Palette: warm dusk town, saturated but soft. No emojis, ever.

#include "world.h"
#include <fstream>
#include <sstream>
#include <cmath>

namespace dw {

static std::string readFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static Color C(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

bool World::load(const std::string& dir) {
  buildTown(parseJson(readFile(dir + "/town.json")));
  JVal iv = parseJson(readFile(dir + "/interiors.json"));
  for (auto& m : iv.at("interiors").arr) buildInterior(m);
  return true;
}

const TileMap& World::map(const std::string& id) const {
  static TileMap empty;
  auto it = maps_.find(id);
  return it == maps_.end() ? empty : it->second;
}

void World::buildTown(const JVal& j) {
  TileMap m;
  m.id = j.s("id"); m.name = j.s("name");
  const JVal& sz = j.at("size");
  m.w = sz.arr.size() > 0 ? (int)sz.arr[0].num : 60;
  m.h = sz.arr.size() > 1 ? (int)sz.arr[1].num : 42;
  m.tile = j.i("tile", 32);
  m.tiles.assign(m.w * m.h, Tile::Grass);
  m.spawn = {(float)j.at("spawn").i("x", 7), (float)j.at("spawn").i("y", 15)};

  auto setTile = [&](int x, int y, Tile t) {
    if (x >= 0 && y >= 0 && x < m.w && y < m.h) m.tiles[y * m.w + x] = t;
  };
  for (auto& r : j.at("rects").arr) {
    Tile t = Tile::Grass;
    std::string tn = r.s("tile");
    if (tn == "path") t = Tile::Path;
    else if (tn == "plaza") t = Tile::Plaza;
    else if (tn == "water") t = Tile::Water;
    int x = r.i("x"), y = r.i("y"), w = r.i("w"), h = r.i("h");
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) setTile(xx, yy, t);
  }
  // Buildings become solid wall tiles (except the door tile).
  for (auto& b : j.at("buildings").arr) {
    Building bd;
    bd.id = b.s("id"); bd.name = b.s("name");
    int x = b.i("x"), y = b.i("y"), w = b.i("w"), h = b.i("h");
    bd.rect = {(float)x, (float)y, (float)w, (float)h};
    const JVal& wall = b.at("wall"), roof = b.at("roof");
    bd.wall = {(uint8_t)wall.arr[0].num, (uint8_t)wall.arr[1].num, (uint8_t)wall.arr[2].num};
    bd.roof = {(uint8_t)roof.arr[0].num, (uint8_t)roof.arr[1].num, (uint8_t)roof.arr[2].num};
    bd.doorX = b.at("door").i("x"); bd.doorY = b.at("door").i("y");
    bd.target = b.at("door").s("target");
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) setTile(xx, yy, Tile::Wall);
    m.buildings.push_back(bd);
  }
  for (auto& t : j.at("trees").arr)
    setTile((int)t.arr[0].num, (int)t.arr[1].num, Tile::Tree);
  for (auto& f : j.at("flowers").arr)
    setTile((int)f.arr[0].num, (int)f.arr[1].num, Tile::Flower);
  for (auto& in : j.at("interactables").arr) {
    Interactable it;
    it.id = in.s("id"); it.kind = in.s("kind"); it.label = in.s("label");
    it.mapId = "town";
    int tx = in.i("x"), ty = in.i("y");
    it.rect = {(float)tx * m.tile, (float)ty * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  // Door mats as interactables.
  for (auto& bd : m.buildings) {
    Interactable it;
    it.id = "door_" + bd.id; it.kind = "door"; it.label = bd.name;
    it.mapId = "town"; it.target = bd.target;
    it.rect = {(float)bd.doorX * m.tile, (float)bd.doorY * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  maps_["town"] = m;
}

void World::buildInterior(const JVal& j) {
  TileMap m;
  m.id = j.s("id"); m.name = j.s("name");
  m.w = j.i("w", 14); m.h = j.i("h", 10); m.tile = 32;
  m.isInterior = true;
  m.floor = j.s("floor", "wood");
  m.tiles.assign(m.w * m.h, Tile::Grass);
  // Walls around the border.
  for (int x = 0; x < m.w; x++) { m.tiles[x] = Tile::Wall; m.tiles[(m.h - 1) * m.w + x] = Tile::Wall; }
  for (int y = 0; y < m.h; y++) { m.tiles[y * m.w] = Tile::Wall; m.tiles[y * m.w + m.w - 1] = Tile::Wall; }
  const JVal& d = j.at("door");
  m.door = {d.s("label"), d.s("target"), d.i("x"), d.i("y")};
  // Door tile itself is walkable (exit mat).
  m.tiles[m.door.y * m.w + m.door.x] = Tile::Path;
  // Also open the wall tile just inside the door so the player can step in.
  if (m.door.y - 1 > 0) m.tiles[(m.door.y - 1) * m.w + m.door.x] = Tile::Path;
  for (auto& f : j.at("furniture").arr) {
    TileMap::Furn fu{f.s("label"), f.s("tile"), f.i("x"), f.i("y")};
    m.furniture.push_back(fu);
    m.tiles[fu.y * m.w + fu.x] = Tile::Wall;  // furniture blocks
  }
  for (auto& c : j.at("computers").arr) {
    m.computers.push_back({c.i("x"), c.i("y")});
    m.tiles[c.i("y") * m.w + c.i("x")] = Tile::Wall;
    Interactable it;
    it.id = "comp_" + m.id + "_" + std::to_string(c.i("x")) + "_" + std::to_string(c.i("y"));
    it.kind = "computer"; it.label = "Computer";
    it.mapId = m.id;
    it.rect = {(float)c.i("x") * m.tile, (float)c.i("y") * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  {
    Interactable it;
    it.id = "exit_" + m.id; it.kind = "door"; it.label = m.door.label;
    it.mapId = m.id; it.target = m.door.target;
    it.rect = {(float)m.door.x * m.tile, (float)m.door.y * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  maps_[m.id] = m;
}

Rect World::tileRect(const std::string& mapId, int tx, int ty) const {
  const TileMap& m = map(mapId);
  return {(float)tx * m.tile, (float)ty * m.tile, (float)m.tile, (float)m.tile};
}

// ---------- rendering ----------

static void fillRect(SDL_Renderer* ren, float x, float y, float w, float h, Color c) {
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  SDL_Rect r{(int)x, (int)y, (int)w, (int)h};
  SDL_RenderFillRect(ren, &r);
}
static void circle(SDL_Renderer* ren, float cx, float cy, float rad, Color c) {
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  for (int w = 0; w < (int)(rad * 2); w++)
    for (int hh = 0; hh < (int)(rad * 2); hh++) {
      float dx = rad - w, dy = rad - hh;
      if (dx * dx + dy * dy <= rad * rad)
        SDL_RenderDrawPoint(ren, (int)(cx - rad + w), (int)(cy - rad + hh));
    }
}

void World::drawText(SDL_Renderer* ren, TTF_Font* font, const std::string& text,
                     int x, int y, Color c) {
  if (!font || text.empty()) return;
  SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), {c.r, c.g, c.b, c.a});
  if (!s) return;
  SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
  SDL_Rect r{x, y, s->w, s->h};
  SDL_RenderCopy(ren, t, nullptr, &r);
  SDL_DestroyTexture(t);
  SDL_FreeSurface(s);
}

void World::drawPlayerNpc(SDL_Renderer* ren, float x, float y, const Color& skin,
                          const Color& shirt, const Color& pants,
                          const std::string& name, TTF_Font* font,
                          bool facingLeft, float bob) {
  float by = y + (bob > 0 ? -2.0f * fabsf(sinf(bob * 6.0f)) : 0);
  // shadow
  { SDL_SetRenderDrawColor(ren, 0, 0, 0, 60); SDL_Rect r{(int)(x - 9), (int)(y + 22), 18, 6};
    SDL_RenderFillRect(ren, &r); }
  // legs
  fillRect(ren, x - 7, by + 10, 6, 12, pants);
  fillRect(ren, x + 1, by + 10, 6, 12, pants);
  // torso
  fillRect(ren, x - 8, by - 4, 16, 15, shirt);
  // head
  circle(ren, x, by - 10, 8, skin);
  // hair cap
  fillRect(ren, x - 8, by - 19, 16, 5, C(45, 35, 28));
  // name
  if (!name.empty() && font) {
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, name.c_str(), {240, 240, 245, 255});
    if (s) {
      SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
      // dark pill behind name
      SDL_SetRenderDrawColor(ren, 10, 14, 20, 170);
      SDL_Rect bg{(int)(x - s->w / 2 - 4), (int)(by - 38), s->w + 8, s->h + 4};
      SDL_RenderFillRect(ren, &bg);
      SDL_Rect r{(int)(x - s->w / 2), (int)(by - 36), s->w, s->h};
      SDL_RenderCopy(ren, t, nullptr, &r);
      SDL_DestroyTexture(t);
      SDL_FreeSurface(s);
    }
  }
  (void)facingLeft;
}

void World::render(SDL_Renderer* ren, TTF_Font* font, const std::string& mapId,
                   const Vec2& cam, int vw, int vh, float dayT, double animT) {
  const TileMap& m = map(mapId);
  int T = m.tile;
  int x0 = (int)(cam.x / T) - 1, x1 = (int)((cam.x + vw) / T) + 1;
  int y0 = (int)(cam.y / T) - 1, y1 = (int)((cam.y + vh) / T) + 1;

  Color grassA = m.isInterior
      ? (m.floor == "wood" ? C(168, 132, 96) : m.floor == "lab" ? C(120, 150, 180) : C(150, 140, 130))
      : C(86, 158, 94);
  Color grassB = m.isInterior ? C(grassA.r - 12, grassA.g - 12, grassA.b - 12)
                              : C(78, 148, 86);

  for (int ty = y0; ty <= y1; ty++) {
    for (int tx = x0; tx <= x1; tx++) {
      float px = tx * T - cam.x, py = ty * T - cam.y;
      Tile t = m.at(tx, ty);
      Color c = ((tx + ty) % 2 == 0) ? grassA : grassB;
      if (t == Tile::Path) c = C(214, 186, 140);
      else if (t == Tile::Plaza) c = C(206, 176, 132);
      else if (t == Tile::Water) c = C(70, 130, 200);
      else if (t == Tile::Wall) continue;  // buildings drawn separately
      fillRect(ren, px, py, T, T, c);
      if (t == Tile::Water) {
        // animated shimmer
        float s = sinf((float)animT * 2 + tx * 0.7f + ty * 0.4f);
        fillRect(ren, px + 6 + s * 3, py + 10, 12, 3, C(140, 190, 240, 160));
      } else if (t == Tile::Tree) {
        fillRect(ren, px, py, T, T, ((tx + ty) % 2 == 0) ? grassA : grassB);
        circle(ren, px + 16, py + 12, 13, C(46, 110, 58));
        circle(ren, px + 10, py + 8, 8, C(60, 130, 70));
        fillRect(ren, px + 14, py + 20, 4, 10, C(110, 80, 55));
      } else if (t == Tile::Flower) {
        fillRect(ren, px, py, T, T, ((tx + ty) % 2 == 0) ? grassA : grassB);
        Color fc = ((tx * 7 + ty * 3) % 3 == 0) ? C(240, 120, 150) : ((tx + ty) % 2 ? C(250, 220, 120) : C(200, 140, 240));
        circle(ren, px + 16, py + 16, 3, fc);
      }
    }
  }

  // Buildings (exterior): wall block + roof cap + door mat + name.
  for (auto& b : m.buildings) {
    float px = b.rect.x * T - cam.x, py = b.rect.y * T - cam.y;
    float pw = b.rect.w * T, ph = b.rect.h * T;
    fillRect(ren, px, py + 10, pw, ph - 10, b.wall);
    fillRect(ren, px - 4, py - 6, pw + 8, 26, b.roof);  // roof overhang
    // door mat (glowing)
    float dx = b.doorX * T - cam.x, dy = b.doorY * T - cam.y;
    float pulse = 0.6f + 0.4f * sinf((float)animT * 3);
    fillRect(ren, dx + 4, dy + 4, T - 8, T - 8, C(45, 212, 191, (uint8_t)(120 * pulse)));
    drawText(ren, font, b.name, (int)(px + pw / 2 - b.name.size() * 3.2f), (int)(py - 24), C(245, 245, 250));
  }

  // Furniture (interiors).
  for (auto& f : m.furniture) {
    float px = f.x * T - cam.x, py = f.y * T - cam.y;
    if (f.tile == "bed") {
      fillRect(ren, px + 2, py + 4, T - 4, T - 8, C(90, 120, 200));
      fillRect(ren, px + 2, py + 4, T - 4, 8, C(240, 240, 245));
    } else if (f.tile == "shelf" || f.tile == "shelves") {
      fillRect(ren, px + 2, py + 2, T - 4, T - 4, C(150, 110, 80));
      fillRect(ren, px + 4, py + 8, T - 8, 4, C(200, 160, 110));
      fillRect(ren, px + 4, py + 18, T - 8, 4, C(200, 160, 110));
    } else if (f.tile == "counter" || f.tile == "bench") {
      fillRect(ren, px, py + 8, T, T - 8, C(170, 140, 110));
      fillRect(ren, px, py + 8, T, 5, C(200, 170, 130));
    } else if (f.tile == "bigboard") {
      fillRect(ren, px - T, py, T * 3, T * 2 - 6, C(120, 90, 60));
      fillRect(ren, px - T + 4, py + 4, T * 3 - 8, T * 2 - 14, C(250, 246, 235));
      for (int i = 0; i < 4; i++)
        fillRect(ren, px - T + 8 + i * 20, py + 10, 14, 10, C(45, 212, 191, 200));
    } else if (f.tile == "notices") {
      fillRect(ren, px + 2, py + 2, T - 4, T - 4, C(180, 160, 120));
    } else if (f.tile == "rack") {
      fillRect(ren, px + 4, py + 2, T - 8, T - 4, C(60, 70, 90));
      for (int i = 0; i < 3; i++) circle(ren, px + 10 + i * 6, py + 10, 2, C(45, 212, 191));
    } else if (f.tile == "board") {
      fillRect(ren, px + 2, py + 2, T - 4, T - 4, C(250, 246, 235));
      fillRect(ren, px + 2, py + 2, T - 4, 6, C(120, 90, 60));
    }
  }
  // Computers.
  for (auto& cp : m.computers) {
    float px = cp.x * T - cam.x, py = cp.y * T - cam.y;
    fillRect(ren, px + 2, py + 14, T - 4, 10, C(90, 100, 115));       // desk
    fillRect(ren, px + 6, py + 2, T - 12, 12, C(30, 36, 48));          // monitor
    float gl = 0.5f + 0.5f * sinf((float)animT * 2 + cp.x);
    fillRect(ren, px + 8, py + 4, T - 16, 8, C(45, 212, 191, (uint8_t)(150 + 80 * gl)));  // screen glow
  }

  // String lights (exterior cozy): warm dots between lamp posts.
  if (!m.isInterior) {
    for (int i = 0; i < 40; i++) {
      float lx = (i * 97 % (m.w * T)) - cam.x;
      float ly = ((i * 57) % (m.h * T)) - cam.y;
      float tw = 0.5f + 0.5f * sinf((float)animT * 2 + i);
      if (dayT < 0.35f || dayT > 0.75f)  // dusk/night only
        circle(ren, lx, ly, 2, C(255, 210, 130, (uint8_t)(140 + 90 * tw)));
    }
  }

  // Day/night tint. dayT: 0=midnight .. 0.5=noon. Warm dusk at start (0.72).
  float dark = 0;
  if (dayT < 0.25f || dayT > 0.8f) dark = 0.45f;
  else if (dayT < 0.35f) dark = 0.45f * (0.35f - dayT) / 0.1f;
  else if (dayT > 0.7f) dark = 0.45f * (dayT - 0.7f) / 0.1f;
  // dusk warmth
  if (dark > 0) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    fillRect(ren, 0, 0, vw, vh, C(18, 22, 58, (uint8_t)(dark * 255)));
    if (dayT > 0.68f && dayT < 0.82f)
      fillRect(ren, 0, 0, vw, vh, C(255, 150, 80, 26));  // dusk warmth
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  }
}

}  // namespace dw
