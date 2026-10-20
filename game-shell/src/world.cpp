// world.cpp — map loading from JSON + 2D pixel-art rendering.
//
// Art direction: modern top-down pixel art, vibrant but balanced. Rich greens
// outdoors, warm wood and lamplight indoors, slate city architecture, clean
// 2px outlines and small readable props. Everything is procedural (no image
// assets), so the build stays self-contained and the look ships out of the box.

#include "world.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace dw {

static std::string readFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ---------- small helpers ----------

static Color C(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

static Color shade(Color c, int d) {
  int r = std::max(0, std::min(255, (int)c.r + d));
  int g = std::max(0, std::min(255, (int)c.g + d));
  int b = std::max(0, std::min(255, (int)c.b + d));
  return {(uint8_t)r, (uint8_t)g, (uint8_t)b, c.a};
}

static Color mix(Color a, Color b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  return {(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
          (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t)};
}

static int rnd(int v) { return (int)std::lround((double)v); }

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
static uint32_t h3(int x, int y, int s = 0) {
  return hash32((uint32_t)(x * 374761393 + y * 668265263 + s * 2147483647));
}

// A crisp integer rectangle. Coordinates are snapped so the art reads as
// pixel art rather than anti-aliased rectangles.
static void pr(SDL_Renderer* ren, int x, int y, int w, int h, Color c) {
  if (w <= 0 || h <= 0 || c.a == 0) return;
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  SDL_Rect r{x, y, w, h};
  SDL_RenderFillRect(ren, &r);
}
static void pframe(SDL_Renderer* ren, int x, int y, int w, int h, Color c, int t = 2) {
  pr(ren, x, y, w, t, c);
  pr(ren, x, y + h - t, w, t, c);
  pr(ren, x, y, t, h, c);
  pr(ren, x + w - t, y, t, h, c);
}
static void pcircle(SDL_Renderer* ren, int cx, int cy, int rad, Color c) {
  if (rad <= 0 || c.a == 0) return;
  SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
  int r2 = rad * rad;
  for (int y = -rad; y <= rad; y++) {
    int span = (int)std::sqrt((float)(r2 - y * y));
    if (span <= 0) continue;
    SDL_Rect row{cx - span, cy + y, span * 2, 1};
    SDL_RenderFillRect(ren, &row);
  }
}

static bool isOpenTile(Tile t) { return t != Tile::Wall; }
static bool isPaved(Tile t) { return t == Tile::Path || t == Tile::Plaza; }

// ---------- tiles ----------

static void drawGrass(SDL_Renderer* ren, const TileMap& m, int tx, int ty,
                      int px, int py, int T, Color a, Color b) {
  uint32_t h = h3(tx, ty, 1);
  Color base = (h & 1) ? a : b;
  pr(ren, px, py, T, T, base);

  // Fine pixel noise keeps large lawns from looking flat.
  for (int i = 0; i < 7; i++) {
    int x = px + (int)(h3(tx, ty, 10 + i) % (uint32_t)T);
    int y = py + (int)(h3(tx, ty, 40 + i) % (uint32_t)T);
    Color n = (i & 1) ? shade(base, 9) : shade(base, -11);
    pr(ren, x, y, 2, 2, n);
  }
  // Short blades.
  if (h % 5 == 0) {
    int gx = px + 6 + (int)(h % 17), gy = py + 8 + (int)((h >> 4) % 16);
    pr(ren, gx, gy, 2, 5, shade(base, -16));
    pr(ren, gx + 2, gy + 1, 2, 4, shade(base, 8));
  }
  // Wildflower speckles, sparse and seasonal.
  if (h % 23 == 0) {
    int fx = px + 8 + (int)((h >> 3) % 15), fy = py + 8 + (int)((h >> 7) % 14);
    Color petal = (h % 46 == 0) ? C(244, 214, 118) : C(236, 168, 198);
    pr(ren, fx, fy, 3, 3, petal);
    pr(ren, fx + 1, fy + 1, 1, 1, C(250, 246, 220));
  }

  // Soft edge where grass meets pavement or water.
  if (isPaved(m.at(tx, ty - 1))) pr(ren, px, py, T, 3, shade(base, -18));
  if (isPaved(m.at(tx, ty + 1))) pr(ren, px, py + T - 3, T, 3, shade(base, -18));
  if (isPaved(m.at(tx - 1, ty))) pr(ren, px, py, 3, T, shade(base, -18));
  if (isPaved(m.at(tx + 1, ty))) pr(ren, px + T - 3, py, 3, T, shade(base, -18));
}

static void drawPath(SDL_Renderer* ren, int tx, int ty, int px, int py, int T) {
  Color base = C(202, 176, 137);
  pr(ren, px, py, T, T, base);
  // Cobbles: two staggered rows, each stone with a highlight and a seam.
  int half = T / 2;
  for (int row = 0; row < 2; row++) {
    int off = ((tx + row + ty) & 1) ? half / 2 : 0;
    for (int col = -1; col < 3; col++) {
      int bx = px + col * half + off;
      int by = py + row * half;
      uint32_t h = h3(tx * 3 + col, ty * 3 + row, 5);
      Color stone = shade(base, (int)(h % 13) - 6);
      int x0 = std::max(px, bx), x1 = std::min(px + T, bx + half - 2);
      int y0 = std::max(py, by), y1 = std::min(py + T, by + half - 2);
      if (x1 <= x0 || y1 <= y0) continue;
      pr(ren, x0, y0, x1 - x0, y1 - y0, stone);
      pr(ren, x0, y0, x1 - x0, 1, shade(stone, 12));
      pr(ren, x0, y0, 1, y1 - y0, shade(stone, 8));
      pr(ren, x0, y1 - 1, x1 - x0, 1, shade(stone, -18));
    }
  }
  // A few pebbles.
  uint32_t h = h3(tx, ty, 21);
  if (h % 4 == 0) {
    int x = px + 5 + (int)(h % 20), y = py + 5 + (int)((h >> 6) % 20);
    pr(ren, x, y, 3, 2, C(166, 143, 110));
    pr(ren, x, y, 2, 1, C(216, 194, 158));
  }
}

static void drawPlaza(SDL_Renderer* ren, int tx, int ty, int px, int py, int T) {
  Color base = C(188, 181, 164);
  pr(ren, px, py, T, T, base);
  // Large contemporary flagstones with generous grout.
  int half = T / 2;
  for (int row = 0; row < 2; row++)
    for (int col = 0; col < 2; col++) {
      uint32_t h = h3(tx * 2 + col, ty * 2 + row, 9);
      Color stone = shade(base, (int)(h % 9) - 4);
      pr(ren, px + col * half + 1, py + row * half + 1, half - 2, half - 2, stone);
      pr(ren, px + col * half + 1, py + row * half + 1, half - 2, 1, shade(stone, 10));
      pr(ren, px + col * half + 1, py + row * half + 1, 1, half - 2, shade(stone, 6));
    }
  // Sparse teal inlay pavers: a modern plaza signature, flush with the grout.
  if (h3(tx, ty, 33) % 19 == 0) {
    pr(ren, px + 2, py + 2, T - 4, T - 4, C(88, 140, 136));
    pr(ren, px + 4, py + 4, T - 8, T - 8, C(112, 176, 168));
    pr(ren, px + 4, py + 4, T - 8, 2, C(142, 202, 192));
    pr(ren, px + T / 2 - 3, py + T / 2 - 3, 6, 6, C(84, 140, 136));
    pr(ren, px + T / 2 - 1, py + T / 2 - 1, 2, 2, C(162, 216, 204));
  }
}

static void drawWater(SDL_Renderer* ren, const TileMap& m, int tx, int ty,
                      int px, int py, int T, double animT) {
  Color deep = C(40, 96, 158), mid = C(56, 124, 186), light = C(94, 164, 216);
  pr(ren, px, py, T, T, deep);
  // Animated bands.
  for (int i = 0; i < 3; i++) {
    float s = sinf((float)animT * 1.6f + tx * 0.8f + ty * 0.5f + i * 1.9f);
    int yy = py + 5 + i * 9 + (int)(s * 2);
    int xx = px + 4 + (int)((1 + s) * 4) + (i & 1) * 6;
    pr(ren, xx, yy, 12 + (i & 1) * 4, 2, i == 1 ? mid : light);
  }
  // Sparkle.
  if (h3(tx, ty, 4) % 3 == 0) {
    int x = px + 6 + (int)(h3(tx, ty, 8) % 18), y = py + 6 + (int)(h3(tx, ty, 9) % 18);
    float tw = 0.5f + 0.5f * sinf((float)animT * 2.4f + (float)tx);
    pr(ren, x, y, 3, 1, C(202, 232, 255, (uint8_t)(130 + 90 * tw)));
  }
  // Foam at every shore edge.
  auto shore = [&](int edge) {
    Color foam = C(174, 216, 238, 220), foam2 = C(132, 188, 224, 170);
    if (edge == 0) { pr(ren, px, py, T, 4, foam2); pr(ren, px, py, T, 2, foam); }
    if (edge == 1) { pr(ren, px, py + T - 4, T, 4, foam2); pr(ren, px, py + T - 2, T, 2, foam); }
    if (edge == 2) { pr(ren, px, py, 4, T, foam2); pr(ren, px, py, 2, T, foam); }
    if (edge == 3) { pr(ren, px + T - 4, py, 4, T, foam2); pr(ren, px + T - 2, py, 2, T, foam); }
  };
  if (m.at(tx, ty - 1) != Tile::Water) shore(0);
  if (m.at(tx, ty + 1) != Tile::Water) shore(1);
  if (m.at(tx - 1, ty) != Tile::Water) shore(2);
  if (m.at(tx + 1, ty) != Tile::Water) shore(3);
}

static void drawTree(SDL_Renderer* ren, int tx, int ty, int px, int py, int T) {
  uint32_t h = h3(tx, ty, 71);
  int baseY = py + T - 2;
  // Shadow.
  pr(ren, px + T / 2 - 15, baseY - 3, 30, 6, C(18, 40, 26, 80));
  // Trunk with a fork and root flare.
  pr(ren, px + T / 2 - 4, baseY - 16, 8, 16, C(92, 64, 43));
  pr(ren, px + T / 2 - 2, baseY - 16, 3, 16, C(124, 90, 58));
  pr(ren, px + T / 2 - 7, baseY - 3, 14, 3, C(76, 52, 36));
  pr(ren, px + T / 2 + 2, baseY - 14, 7, 3, C(92, 64, 43));

  // One tree in five is a conifer: a different silhouette keeps the green
  // areas from reading as rows of identical shrubs.
  if (h % 5 == 0) {
    int cx = px + T / 2;
    for (int i = 0; i < 4; i++) {
      int ry = py + 6 + i * 8;
      int half = 20 - i * 4;
      for (int yy = 0; yy < 9; yy++) {
        int span = (int)(half * (1.0f - yy / 11.0f));
        pr(ren, cx - span, ry + yy, span * 2, 1, i == 3 ? C(56, 118, 66) : C(40, 96, 56));
        pr(ren, cx - span, ry + yy, span, 1, C(52, 112, 64));
      }
    }
    pr(ren, cx - 1, py + 3, 2, 4, C(78, 142, 82));
    return;
  }

  // Canopy: layered pixel circles in three greens.
  int cx = px + T / 2 + (int)(h % 5) - 2;
  int cy = py + 7 + (int)((h >> 4) % 3);
  int rad = 18 + (int)((h >> 8) % 3);
  pcircle(ren, cx, cy + 3, rad, C(25, 70, 44));
  pcircle(ren, cx - 2, cy + 1, rad - 2, C(40, 101, 58));
  pcircle(ren, cx + 4, cy - 1, rad - 5, C(56, 128, 72));
  pcircle(ren, cx - 6, cy - 5, rad - 8, C(78, 154, 86));
  pcircle(ren, cx + 1, cy - 9, rad - 12, C(98, 174, 98));
  // Leaf highlights and a couple of dark gaps.
  for (int i = 0; i < 9; i++) {
    int lx = cx - rad / 2 + (int)(h3(tx, ty, 80 + i) % (uint32_t)(rad + 6));
    int ly = cy - rad / 2 + (int)(h3(tx, ty, 90 + i) % (uint32_t)(rad + 4));
    pr(ren, lx, ly, 2, 2, C(128, 192, 108));
  }
  pr(ren, cx + 2, cy + 6, 5, 2, C(28, 80, 47));
  pr(ren, cx - 7, cy + 1, 4, 2, C(32, 88, 52));
}

static void drawFlowerTile(SDL_Renderer* ren, int tx, int ty, int px, int py, int T,
                           Color a, Color b) {
  drawGrass(ren, TileMap{}, tx, ty, px, py, T, a, b);  // empty map = no edges
  uint32_t h = h3(tx, ty, 44);
  static const Color petals[4] = {C(240, 132, 158), C(246, 214, 110),
                                  C(190, 146, 236), C(246, 246, 236)};
  for (int i = 0; i < 3; i++) {
    int fx = px + 6 + (int)(h3(tx, ty, 50 + i) % 20);
    int fy = py + 6 + (int)(h3(tx, ty, 60 + i) % 18);
    Color petal = petals[(h + i) % 4];
    pr(ren, fx + 1, fy + 3, 1, 4, C(46, 104, 54));
    pr(ren, fx, fy, 3, 3, petal);
    pr(ren, fx + 1, fy + 1, 1, 1, C(252, 238, 172));
  }
}

// ---------- interiors ----------

static void drawFloorTile(SDL_Renderer* ren, const TileMap& m, int tx, int ty,
                          int px, int py, int T, Color grassA, Color grassB) {
  if (m.floor == "wood") {
    Color base = C(158, 112, 74);
    pr(ren, px, py, T, T, base);
    int plank = T / 2;
    for (int row = 0; row < 2; row++) {
      int off = (ty & 1) ? -plank / 2 : 0;
      for (int col = -1; col < 3; col++) {
        int bx = px + col * plank + off;
        int x0 = std::max(px, bx), x1 = std::min(px + T, bx + plank - 2);
        int y0 = py + row * plank;
        if (x1 <= x0) continue;
        uint32_t h = h3(tx * 2 + col, ty * 2 + row, 3);
        Color shadeCol = shade(base, (int)(h % 17) - 8);
        pr(ren, x0, y0, x1 - x0, plank - 1, shadeCol);
        pr(ren, x0, y0, x1 - x0, 1, shade(shadeCol, 12));
        pr(ren, x0, y0 + plank - 2, x1 - x0, 1, C(112, 76, 48));
        // Grain.
        int gy = y0 + 5 + (int)((h >> 4) % (uint32_t)(plank - 8));
        pr(ren, x0 + 4, gy, std::max(4, x1 - x0 - 10), 1, shade(shadeCol, -8));
      }
    }
  } else if (m.floor == "lab") {
    Color a = C(196, 212, 218), b = C(178, 196, 206);
    int half = T / 2;
    for (int row = 0; row < 2; row++)
      for (int col = 0; col < 2; col++) {
        Color c = ((tx * 2 + col + ty * 2 + row) & 1) ? a : b;
        pr(ren, px + col * half, py + row * half, half, half, c);
        pr(ren, px + col * half, py + row * half, half, 1, shade(c, 10));
      }
  } else if (m.floor == "shop") {
    Color a = C(188, 174, 156), b = C(174, 160, 144);
    int half = T / 2;
    for (int row = 0; row < 2; row++)
      for (int col = 0; col < 2; col++) {
        Color c = ((tx + col + ty + row) & 1) ? a : b;
        pr(ren, px + col * half, py + row * half, half - 1, half - 1, c);
      }
  } else if (m.floor == "hall") {
    Color base = C(166, 138, 104);
    pr(ren, px, py, T, T, base);
    pr(ren, px, py, T, 2, C(188, 160, 122));
    pr(ren, px, py, 2, T, C(182, 154, 118));
    pr(ren, px + T / 2, py, 1, T, C(140, 114, 84));
    pr(ren, px, py + T / 2, T, 1, C(140, 114, 84));
  } else {
    pr(ren, px, py, T, T, grassA);
    if (((tx + ty) & 1) == 0) pr(ren, px, py, T, T, grassB);
    for (int i = 0; i < 5; i++) {
      int x = px + (int)(h3(tx, ty, 120 + i) % (uint32_t)T);
      int y = py + (int)(h3(tx, ty, 130 + i) % (uint32_t)T);
      pr(ren, x, y, 2, 2, shade(grassA, (i & 1) ? 6 : -6));
    }
  }
}

static void drawWallTile(SDL_Renderer* ren, const TileMap& m, int tx, int ty,
                         int px, int py, int T, double animT, float dayT) {
  Color face = C(54, 61, 74), dark = C(38, 44, 55), trim = C(78, 88, 104);
  pr(ren, px, py, T, T, face);
  // Vertical beadboard.
  for (int x = 0; x < T; x += 14) pr(ren, px + x, py, 2, T, dark);
  // Highlights on the room-facing edge.
  bool top = (ty == 0);
  bool bottom = (ty == m.h - 1);
  bool left = (tx == 0);
  bool right = (tx == m.w - 1);
  if (top) {
    pr(ren, px, py + T - 6, T, 6, dark);          // baseboard
    pr(ren, px, py + T - 8, T, 2, trim);
  }
  if (bottom) pr(ren, px, py, T, 6, dark);
  if (left) pr(ren, px + T - 6, py, 6, T, dark);
  if (right) pr(ren, px, py, 6, T, dark);

  // Windows on the top wall, with light and curtains.
  if (top && (tx % 4 == 1 || tx % 4 == 2)) {
    int wx = px + T / 2 - 14, wy = py + 8;
    pr(ren, wx - 2, wy - 2, 32, 30, C(30, 34, 44));
    float night = (dayT < 0.28f || dayT > 0.78f) ? 1.0f : 0.0f;
    Color glass = night > 0.5f ? C(84, 88, 112) : C(126, 184, 210);
    pr(ren, wx, wy, 28, 26, glass);
    pr(ren, wx, wy + 10, 28, 2, C(228, 238, 244, 200));
    pr(ren, wx + 13, wy, 2, 26, C(60, 70, 86));
    // Curtains.
    pr(ren, wx - 2, wy, 7, 26, C(198, 150, 112));
    pr(ren, wx + 23, wy, 7, 26, C(198, 150, 112));
    pr(ren, wx - 2, wy, 7, 2, C(222, 180, 138));
    pr(ren, wx + 23, wy, 7, 2, C(222, 180, 138));
  }
  // Framed wall art in a few spots.
  if (top && tx % 7 == 4) {
    int ax = px + 7, ay = py + 10;
    pr(ren, ax - 2, ay - 2, 22, 22, C(34, 38, 48));
    pr(ren, ax, ay, 18, 18, C(226, 218, 196));
    pr(ren, ax + 2, ay + 10, 14, 5, C(96, 150, 126));
    pcircle(ren, ax + 12, ay + 6, 3, C(240, 188, 96));
  }
  // Ceiling lamps at the top wall.
  if (top && tx % 5 == 2) {
    pr(ren, px + T / 2 - 2, py - 2, 4, 9, C(28, 32, 40));
    pr(ren, px + T / 2 - 9, py + 7, 18, 5, C(246, 210, 132));
    pr(ren, px + T / 2 - 6, py + 8, 12, 2, C(255, 240, 190));
  }
  (void)animT;
}

// ---------- furniture / props ----------

static void drawFurniture(SDL_Renderer* ren, const TileMap& m, const TileMap::Furn& f,
                          int px, int py, int T, double animT) {
  uint32_t h = h3(f.x, f.y, 17);
  const std::string& t = f.tile;
  if (t == "bed") {
    pr(ren, px + 2, py + 2, T - 4, T - 4, C(88, 58, 42));          // frame
    pr(ren, px + 5, py + 4, T - 10, 9, C(124, 84, 58));            // headboard
    pr(ren, px + 6, py + 12, T - 12, T - 16, C(240, 232, 216));    // mattress
    pr(ren, px + 8, py + 14, T - 16, 10, C(248, 244, 236));        // pillow
    pr(ren, px + 8, py + 22, T - 16, 2, C(206, 198, 184));
    pr(ren, px + 6, py + 26, T - 12, T - 30, C(96, 132, 172));     // duvet
    pr(ren, px + 6, py + 26, T - 12, 3, C(142, 178, 212));         // fold highlight
    pr(ren, px + 15, py + 34, T - 30, 2, C(78, 110, 148));         // crease
    pr(ren, px + 6, py + T - 6, 4, 3, C(62, 42, 32));              // feet
    pr(ren, px + T - 10, py + T - 6, 4, 3, C(62, 42, 32));
  } else if (t == "shelf" || t == "shelves") {
    pr(ren, px + 2, py + 2, T - 4, T - 4, C(104, 72, 48));
    pr(ren, px + 4, py + 4, T - 8, T - 8, C(138, 98, 64));
    for (int row = 0; row < 3; row++) {
      int sy = py + 7 + row * 8;
      pr(ren, px + 4, sy, T - 8, 2, C(174, 130, 88));
      for (int i = 0; i < 4; i++) {
        Color book = (h3(f.x, f.y, row * 9 + i) & 1)
                         ? C(198, 96, 88) : ((h3(f.x, f.y, row + 30 + i) & 1)
                                                 ? C(90, 150, 170) : C(226, 190, 104));
        pr(ren, px + 6 + i * 6, sy - 5, 4, 6, book);
      }
    }
  } else if (t == "counter" || t == "bench") {
    pr(ren, px, py + 8, T, T - 8, C(116, 78, 52));
    pr(ren, px, py + 8, T, 6, C(182, 142, 100));
    pr(ren, px + 2, py + 9, T - 4, 2, C(214, 176, 128));
    // Espresso machine + cups for counters.
    if (t == "counter") {
      pr(ren, px + 5, py + 14, 14, 10, C(58, 64, 74));
      pr(ren, px + 7, py + 16, 10, 4, C(120, 190, 196));
      pr(ren, px + 8, py + 24, 8, 3, C(42, 46, 56));
      pr(ren, px + 22, py + 16, 6, 5, C(236, 232, 222));
      pr(ren, px + 23, py + 15, 4, 1, C(200, 196, 186));
      // Steam.
      float s = sinf((float)animT * 2.0f + f.x);
      pr(ren, px + 10 + (int)(s * 2), py + 10, 2, 3, C(236, 240, 244, 170));
    }
    if (t == "bench") {
      pr(ren, px + 4, py + 16, 8, 7, C(70, 80, 96));
      pr(ren, px + 5, py + 17, 6, 4, C(104, 188, 184));
      pr(ren, px + 18, py + 17, 6, 5, C(214, 214, 208));
    }
  } else if (t == "bigboard") {
    pr(ren, px - T, py - 2, T * 3, T * 2, C(84, 62, 46));
    pr(ren, px - T + 4, py + 2, T * 3 - 8, T * 2 - 8, C(240, 238, 228));
    pr(ren, px - T + 8, py + 8, T * 3 - 16, 2, C(180, 190, 194));
    // Simple architecture sketch + sticky notes.
    pr(ren, px - T + 12, py + 20, 24, 16, C(126, 186, 188));
    pr(ren, px - T + 20, py + 28, 38, 3, C(96, 150, 156));
    pr(ren, px + 8, py + 18, 10, 8, C(246, 206, 110));
    pr(ren, px + 22, py + 26, 10, 8, C(236, 150, 150));
    pr(ren, px + 36, py + 16, 10, 8, C(168, 208, 236));
  } else if (t == "board" || t == "notices") {
    pr(ren, px + 2, py + 2, T - 4, T - 4, C(112, 80, 52));
    pr(ren, px + 4, py + 4, T - 8, T - 8, t == "notices" ? C(196, 168, 116) : C(244, 242, 232));
    for (int i = 0; i < 3; i++) {
      Color note = (i == 0) ? C(246, 206, 110) : (i == 1) ? C(156, 214, 198) : C(238, 170, 170);
      pr(ren, px + 6 + i * 7, py + 7 + (i & 1) * 9, 6, 7, note);
    }
  } else if (t == "couch") {
    // Living-room sofa: wooden backboard, two teal cushions, stubby feet.
    pr(ren, px + 4, py + 8, T - 8, T - 18, C(62, 44, 34));
    pr(ren, px + 5, py + 6, T - 10, 12, C(88, 64, 46));
    pr(ren, px + 7, py + 8, T - 14, 8, C(54, 132, 126));
    pr(ren, px + 7, py + 9, T - 14, 2, C(104, 196, 180));
    pr(ren, px + 5, py + 18, T - 10, 20, C(70, 92, 96));
    pr(ren, px + 8, py + 19, (T - 20) / 2, 17, C(84, 172, 158));
    pr(ren, px + 10 + (T - 20) / 2, py + 19, (T - 20) / 2, 17, C(76, 160, 148));
    pr(ren, px + 8, py + 20, (T - 20) / 2, 2, C(140, 214, 200));
    pr(ren, px + 5, py + 19, 4, 20, C(60, 78, 84));
    pr(ren, px + T - 9, py + 19, 4, 20, C(60, 78, 84));
    pr(ren, px + 7, py + 37, 4, 4, C(48, 34, 28));
    pr(ren, px + T - 11, py + 37, 4, 4, C(48, 34, 28));
  } else if (t == "table") {
    // Coffee table with a mug and a small plant.
    pr(ren, px + 4, py + 16, T - 8, T - 26, C(92, 62, 42));
    pr(ren, px + 5, py + 14, T - 10, T - 24, C(170, 122, 78));
    pr(ren, px + 6, py + 15, T - 12, 2, C(212, 162, 110));
    pr(ren, px + 7, py + 17, T - 14, 2, C(132, 92, 60));
    pr(ren, px + 8, py + 19, 11, 8, C(238, 236, 228));
    pr(ren, px + 9, py + 20, 9, 3, C(126, 196, 190));
    pr(ren, px + T - 20, py + 20, 8, 8, C(72, 126, 84));
    pr(ren, px + T - 18, py + 17, 5, 5, C(96, 162, 106));
    pr(ren, px + 9, py + T - 12, 4, 5, C(74, 50, 38));
    pr(ren, px + T - 13, py + T - 12, 4, 5, C(74, 50, 38));
  } else if (t == "rack") {
    pr(ren, px + 2, py + 2, T - 4, T - 4, C(38, 44, 54));
    pr(ren, px + 5, py + 5, T - 10, T - 10, C(54, 62, 76));
    for (int i = 0; i < 4; i++) {
      int ry = py + 7 + i * 6;
      pr(ren, px + 7, ry, T - 14, 3, C(72, 82, 98));
      float blink = 0.5f + 0.5f * sinf((float)animT * 4 + i * 1.7f + f.x);
      pr(ren, px + T - 11, ry, 2, 2, C(70, 230, 190, (uint8_t)(120 + 120 * blink)));
    }
  } else {
    // Generic furniture: a solid, friendly box with a lit edge.
    pr(ren, px + 3, py + 5, T - 6, T - 8, C(130, 98, 70));
    pr(ren, px + 3, py + 5, T - 6, 4, C(176, 138, 98));
    pr(ren, px + 5, py + 11, T - 10, 2, C(98, 72, 52));
  }
}

static void drawComputer(SDL_Renderer* ren, const TileMap& m, const TileMap::Comp& cp,
                         int px, int py, int T, double animT) {
  // Desk.
  pr(ren, px + 2, py + T - 20, T - 4, 14, C(108, 78, 54));
  pr(ren, px + 2, py + T - 20, T - 4, 3, C(166, 128, 90));
  pr(ren, px + 4, py + T - 6, 3, 6, C(76, 54, 40));
  pr(ren, px + T - 7, py + T - 6, 3, 6, C(76, 54, 40));
  // Monitor.
  pr(ren, px + 6, py + 5, T - 12, 15, C(30, 36, 48));
  pr(ren, px + 8, py + 7, T - 16, 10, C(26, 42, 50));
  float gl = 0.5f + 0.5f * sinf((float)animT * 2.2f + cp.x * 1.3f);
  Color screen = C(58, 218, 194, (uint8_t)(150 + 80 * gl));
  pr(ren, px + 9, py + 8, T - 18, 8, screen);
  // Code lines.
  pr(ren, px + 11, py + 10, 6, 1, C(226, 255, 246, 220));
  pr(ren, px + 11, py + 12, 10, 1, C(226, 255, 246, 180));
  pr(ren, px + 13, py + 14, 5, 1, C(226, 255, 246, 160));
  pr(ren, px + T / 2 - 2, py + 20, 4, 5, C(54, 60, 72));
  pr(ren, px + T / 2 - 7, py + 25, 14, 2, C(48, 54, 66));
  // Keyboard and mug.
  pr(ren, px + 8, py + T - 15, T - 18, 4, C(62, 70, 84));
  pr(ren, px + 8, py + T - 15, T - 18, 1, C(94, 104, 120));
  pr(ren, px + T - 10, py + T - 18, 5, 5, C(236, 232, 222));
  // Chair.
  pr(ren, px + T / 2 - 8, py + T - 4, 16, 4, C(52, 58, 70));
  pr(ren, px + T / 2 - 7, py + T, 3, 3, C(42, 46, 56));
  pr(ren, px + T / 2 + 4, py + T, 3, 3, C(42, 46, 56));
}

static void drawLamp(SDL_Renderer* ren, int px, int py, int T, int variant) {
  pr(ren, px + T / 2 - 9, py + T - 4, 18, 4, C(20, 30, 34, 80));
  pr(ren, px + T / 2 - 2, py + 5, 4, T - 8, C(42, 52, 58));
  pr(ren, px + T / 2 - 1, py + 8, 1, T - 12, C(78, 92, 96));
  pr(ren, px + T / 2 - 6, py + 2, 12, 6, C(34, 42, 48));
  pr(ren, px + T / 2 - 5, py + 3, 10, 4, C(248, 214, 136));
  if (variant & 1) {
    // Bicycle-ish planter detail? Keep the pole clean; small base.
    pr(ren, px + T / 2 - 5, py + T - 8, 10, 4, C(58, 66, 64));
  }
}

static void drawBench(SDL_Renderer* ren, int px, int py, int T) {
  pr(ren, px + 3, py + T - 8, T - 6, 5, C(18, 28, 30, 70));
  for (int i = 0; i < 3; i++) pr(ren, px + 2, py + 12 + i * 5, T - 4, 3, C(156, 112, 72));
  pr(ren, px + 2, py + 11, T - 4, 2, C(196, 150, 96));
  pr(ren, px + 5, py + 22, 3, 8, C(54, 62, 66));
  pr(ren, px + T - 8, py + 22, 3, 8, C(54, 62, 66));
  pr(ren, px + 2, py + 8, T - 4, 3, C(128, 92, 62));
}

static void drawPlanter(SDL_Renderer* ren, int px, int py, int T) {
  pr(ren, px + 5, py + T - 12, T - 10, 10, C(142, 86, 62));
  pr(ren, px + 5, py + T - 12, T - 10, 3, C(188, 122, 84));
  pcircle(ren, px + T / 2, py + 16, 10, C(46, 112, 62));
  pcircle(ren, px + T / 2 - 3, py + 13, 8, C(64, 140, 76));
  pcircle(ren, px + T / 2 + 2, py + 12, 6, C(86, 164, 90));
  pr(ren, px + T / 2 - 1, py + 20, 2, 6, C(84, 122, 64));
}

static void drawHatch(SDL_Renderer* ren, int px, int py, int T, double animT) {
  pr(ren, px + 2, py + 11, T - 4, T - 13, C(38, 44, 52));
  pr(ren, px + 4, py + 13, T - 8, T - 17, C(72, 82, 92));
  for (int i = 0; i < 5; i++) pr(ren, px + 6 + i * 5, py + 15, 2, 2, C(120, 132, 142));
  pr(ren, px + 5, py + 15, T - 10, 2, C(110, 122, 132));
  float pulse = 0.5f + 0.5f * sinf((float)animT * 2.5f);
  pr(ren, px + T / 2 - 5, py + 24, 10, 2, C(62, 222, 198, (uint8_t)(90 + 100 * pulse)));
}

static void drawInspectProp(SDL_Renderer* ren, const Interactable& it,
                            int px, int py, int T, double animT) {
  const std::string& id = it.id;
  if (id.find("fountain") != std::string::npos) {
    pr(ren, px + 1, py + 10, T - 2, T - 12, C(112, 108, 102));
    pr(ren, px + 4, py + 12, T - 8, T - 16, C(78, 142, 172));
    pr(ren, px + 6, py + 14, T - 12, 3, C(170, 214, 230));
    pr(ren, px + T / 2 - 2, py + 4, 4, 12, C(152, 150, 144));
    pr(ren, px + T / 2 - 5, py + 3, 10, 3, C(186, 182, 172));
    float s = 0.5f + 0.5f * sinf((float)animT * 3.0f);
    pr(ren, px + T / 2 - 5, py + 7 - (int)(s * 2), 2, 5, C(188, 224, 238, 180));
    pr(ren, px + T / 2 + 3, py + 7 + (int)(s * 2), 2, 5, C(188, 224, 238, 160));
  } else if (id.find("stone") != std::string::npos) {
    pr(ren, px + 4, py + 16, T - 8, 12, C(132, 130, 122));
    pr(ren, px + 6, py + 14, T - 12, 5, C(170, 166, 154));
    pr(ren, px + 9, py + 18, T - 18, 2, C(106, 104, 96));
    pr(ren, px + 10, py + 22, 10, 1, C(106, 104, 96));
  } else {
    // Notice board / signpost.
    pr(ren, px + T / 2 - 2, py + 14, 4, T - 16, C(104, 76, 52));
    pr(ren, px + 3, py + 3, T - 6, 14, C(126, 90, 58));
    pr(ren, px + 5, py + 5, T - 10, 10, C(238, 232, 214));
    pr(ren, px + 7, py + 7, 8, 1, C(120, 140, 142));
    pr(ren, px + 7, py + 10, 12, 1, C(120, 140, 142));
    pr(ren, px + T - 9, py + 6, 3, 3, C(78, 190, 174));
  }
}

static void drawInteractable(SDL_Renderer* ren, TTF_Font* font, const TileMap& m,
                             const Interactable& it, int px, int py, int T,
                             double animT) {
  if (it.kind == "door" || it.kind == "computer") return;
  if (it.kind == "board") {
    pr(ren, px + 3, py + 8, 3, T - 9, C(104, 76, 50));
    pr(ren, px + T - 6, py + 8, 3, T - 9, C(104, 76, 50));
    pr(ren, px + 2, py + 2, T - 4, 18, C(88, 62, 44));
    pr(ren, px + 4, py + 4, T - 8, 14, C(242, 238, 224));
    pr(ren, px + 6, py + 6, 8, 2, C(70, 178, 166));
    pr(ren, px + 6, py + 9, 13, 1, C(152, 160, 164));
    pr(ren, px + 6, py + 12, 10, 1, C(152, 160, 164));
    pr(ren, px + 2, py + 2, T - 4, 2, C(52, 196, 180));
  } else if (it.kind == "bus") {
    pr(ren, px + 5, py + 6, 3, T - 8, C(64, 72, 80));
    pr(ren, px + 5, py + 2, 3, 3, C(64, 72, 80));
    pr(ren, px + 3, py + 2, 12, 9, C(38, 48, 64));
    pr(ren, px + 4, py + 3, 10, 7, C(240, 190, 92));
    pr(ren, px + 6, py + 4, 6, 2, C(38, 48, 64));
    pr(ren, px + 6, py + 7, 4, 1, C(38, 48, 64));
    pr(ren, px + 3, py + 13, 22, 3, C(66, 82, 90));
    pr(ren, px + 5, py + 16, 2, 8, C(52, 62, 70));
    pr(ren, px + 21, py + 16, 2, 8, C(52, 62, 70));
  } else if (it.kind == "bank") {
    pr(ren, px + 5, py + 4, T - 10, T - 6, C(44, 54, 64));
    pr(ren, px + 7, py + 6, T - 14, 9, C(34, 70, 74));
    pr(ren, px + 8, py + 7, T - 16, 6, C(86, 218, 196));
    pr(ren, px + 8, py + 18, T - 16, 3, C(88, 98, 110));
    pr(ren, px + 8, py + 23, T - 16, 2, C(88, 98, 110));
    pr(ren, px + 7, py + T - 6, 4, 3, C(30, 36, 44));
    pr(ren, px + T - 11, py + T - 6, 4, 3, C(30, 36, 44));
  } else if (it.kind == "mail") {
    pr(ren, px + T / 2 - 2, py + 16, 4, T - 18, C(84, 92, 100));
    pr(ren, px + 6, py + 5, T - 12, 14, C(52, 100, 162));
    pr(ren, px + 6, py + 5, T - 12, 4, C(78, 136, 198));
    pr(ren, px + 8, py + 11, T - 16, 2, C(34, 64, 106));
    pr(ren, px + T - 10, py + 7, 3, 5, C(220, 108, 96));
  } else if (it.kind == "hatch") {
    drawHatch(ren, px, py, T, animT);
  } else if (it.kind == "inspect") {
    drawInspectProp(ren, it, px, py, T, animT);
  } else if (it.kind == "shop") {
    pr(ren, px + 2, py + 8, T - 4, T - 10, C(122, 92, 60));
    pr(ren, px + 2, py + 3, T - 4, 7, C(206, 120, 96));
    pr(ren, px + 4, py + 5, T - 8, 3, C(238, 206, 158));
    pr(ren, px + 5, py + 14, 7, 7, C(92, 182, 150));
    pr(ren, px + 15, py + 14, 7, 7, C(230, 188, 96));
  } else {
    // Unknown interactable: a small teal-glow marker, never invisible.
    pr(ren, px + T / 2 - 6, py + T / 2 - 6, 12, 12, C(40, 92, 86));
    pr(ren, px + T / 2 - 4, py + T / 2 - 4, 8, 8, C(70, 220, 198));
  }
  (void)font;
  (void)it.label;
}

// ---------- buildings ----------

static void drawBuilding(SDL_Renderer* ren, TTF_Font* font, const TileMap& m,
                         const Building& b, int camX, int camY, double animT, float dayT) {
  int T = m.tile;
  int px = rnd(b.rect.x * T - camX);
  int py = rnd(b.rect.y * T - camY);
  int pw = rnd(b.rect.w * T);
  int ph = rnd(b.rect.h * T);
  if (px + pw < -80 || py + ph < -80 || px > 4000 || py > 3000) return;

  Color wall = b.wall;
  Color roof = b.roof;
  Color roofDark = shade(roof, -26);
  Color roofLight = shade(roof, 16);

  // Ground shadow (offset down-right) and an outline that separates the
  // building from the street.
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  pr(ren, px + 7, py + 9, pw, ph, C(12, 18, 22, 90));
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

  // Facade (front wall) and roof slab.
  int facadeH = std::min(26, ph / 3);
  pr(ren, px, py + 10, pw, ph - 10, shade(wall, -18));
  pr(ren, px, py + 10, pw, 3, shade(wall, 18));

  // Roof with a shallow gable: two slopes meeting at a ridge, shingled and
  // overhanging the walls. Top-down but readable as a building at a glance.
  int roofTop = py - 4;
  int roofH = ph - facadeH + 12;
  int ridgeY = roofTop + roofH / 2;
  pr(ren, px - 5, roofTop, pw + 10, roofH, roofDark);
  pr(ren, px - 2, roofTop + 2, pw + 4, roofH - 4, roof);
  // Upper slope catches more sky; lower slope sits in shade.
  pr(ren, px - 2, roofTop + 2, pw + 4, ridgeY - roofTop - 3, mix(roof, C(255, 238, 205), 0.12));
  pr(ren, px - 2, ridgeY + 2, pw + 4, roofTop + roofH - ridgeY - 4, shade(roof, -12));
  // Ridge cap.
  pr(ren, px - 3, ridgeY - 1, pw + 6, 4, roofDark);
  pr(ren, px - 3, ridgeY - 1, pw + 6, 1, roofLight);
  pr(ren, px - 3, ridgeY + 3, pw + 6, 1, shade(roof, -34));
  // Shingle rows, staggered.
  for (int y = roofTop + 8; y < roofTop + roofH - 6; y += 9) {
    pr(ren, px - 2, y, pw + 4, 1, C(0, 0, 0, 34));
    int off = ((y - roofTop) / 9) & 1 ? 8 : 0;
    for (int x = px + off; x < px + pw - 2; x += 16)
      pr(ren, x, y - 4, 1, 4, shade(roof, -14));
  }
  pr(ren, px - 5, roofTop, pw + 10, 2, shade(roof, -42));
  pr(ren, px - 5, roofTop + roofH - 2, pw + 10, 2, shade(roof, -42));

  // Chimney with an animated wisp, because every good roof has one.
  if (h3((int)b.rect.x, (int)b.rect.y, 34) % 2 == 0) {
    int chx = px + pw - 42;
    int chy = roofTop + 12;
    pr(ren, chx, chy, 14, 18, C(96, 66, 52));
    pr(ren, chx + 2, chy + 2, 10, 14, C(132, 90, 68));
    pr(ren, chx - 1, chy - 3, 16, 5, C(72, 50, 40));
    float drift = sinf((float)animT * 1.2f + b.rect.x) * 3.0f;
    pr(ren, chx + 5 + (int)drift, chy - 10, 4, 5, C(212, 216, 212, 80));
    pr(ren, chx + 7 + (int)(drift * 1.6f), chy - 17, 5, 6, C(212, 216, 212, 55));
  }

  // Rooftop utilities.
  uint32_t h = h3((int)b.rect.x, (int)b.rect.y, 90);
  if (h % 3 == 0) {
    int ux = px + 8 + (int)(h % (uint32_t)std::max(8, pw - 44));
    int uy = roofTop + 12 + (int)((h >> 5) % (uint32_t)std::max(4, roofH - 34));
    pr(ren, ux, uy, 22, 14, C(112, 120, 128));
    pr(ren, ux + 2, uy + 2, 18, 10, C(138, 146, 152));
    pcircle(ren, ux + 11, uy + 7, 4, C(88, 96, 104));
    pr(ren, ux + 10, uy + 6, 2, 2, C(64, 72, 80));
  }
  if (h % 5 == 0 && pw > 90) {
    int sx = px + pw - 46;
    pr(ren, sx, roofTop + 14, 22, 12, C(64, 76, 92));
    pr(ren, sx + 2, roofTop + 16, 18, 8, C(128, 190, 210));
    pr(ren, sx + 10, roofTop + 16, 1, 8, C(84, 106, 124));
  }

  // Facade windows and trim.
  int winY = py + ph - facadeH + 5;
  for (int wx = px + 10; wx < px + pw - 14; wx += 26) {
    Color lit = C(250, 206, 118);
    bool night = (dayT < 0.28f || dayT > 0.77f);
    pr(ren, wx - 1, winY - 1, 13, 11, C(38, 44, 52));
    pr(ren, wx, winY, 11, 9, night ? C(92, 82, 96) : C(118, 166, 186));
    if (night && ((wx / 3) & 1)) pr(ren, wx, winY, 11, 9, lit);
    pr(ren, wx, winY + 4, 11, 1, C(58, 70, 82));
    pr(ren, wx + 5, winY, 1, 9, C(58, 70, 82));
  }

  // Door and awning at the actual door tile.
  int doorPx = rnd(b.doorX * T - camX);
  int doorMatPy = rnd(b.doorY * T - camY);
  int doorBaseY = py + ph - facadeH;
  // Door frame sits on the facade above the mat.
  pr(ren, doorPx + 5, doorBaseY - 4, T - 10, facadeH + 4, C(72, 50, 40));
  pr(ren, doorPx + 7, doorBaseY, T - 14, facadeH - 4, C(120, 80, 54));
  pr(ren, doorPx + 9, doorBaseY + 2, T - 18, facadeH - 8, C(180, 128, 82));
  pr(ren, doorPx + T - 14, doorBaseY + facadeH / 2, 3, 3, C(244, 206, 120));
  // Awning.
  pr(ren, doorPx + 2, doorBaseY - 10, T - 4, 7, C(52, 190, 174));
  for (int ax = doorPx + 4; ax < doorPx + T - 4; ax += 8)
    pr(ren, ax, doorBaseY - 10, 4, 7, C(228, 240, 232));
  pr(ren, doorPx + 2, doorBaseY - 10, T - 4, 2, C(32, 154, 142));

  // Glowing doormat outside.
  float pulse = 0.65f + 0.35f * sinf((float)animT * 2.4f);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  pr(ren, doorPx + 4, doorMatPy + 5, T - 8, T - 10, C(52, 196, 178, (uint8_t)(70 + 50 * pulse)));
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  pr(ren, doorPx + 7, doorMatPy + 8, T - 14, T - 16, C(54, 150, 140));

  // Sign plate over the door, with the building name.
  if (font && !b.name.empty()) {
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, b.name.c_str(), &tw, &th);
    int sw = std::min(pw - 8, tw + 18);
    int sx = doorPx + T / 2 - sw / 2;
    sx = std::max(px + 4, std::min(px + pw - sw - 4, sx));
    int sy = py + ph - facadeH - 34;
    pr(ren, sx, sy, sw, 20, C(26, 32, 40, 235));
    pframe(ren, sx, sy, sw, 20, C(58, 190, 174), 2);
    pr(ren, sx + 4, sy + 4, sw - 8, 1, C(82, 220, 198));
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, b.name.c_str(), {244, 240, 226, 255});
    if (s) {
      SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
      int tx = sx + (sw - s->w) / 2;
      int ty = sy + (20 - s->h) / 2;
      SDL_Rect dst{tx, ty, s->w, s->h};
      SDL_RenderCopy(ren, t, nullptr, &dst);
      SDL_DestroyTexture(t);
      SDL_FreeSurface(s);
    }
  }
}

// ---------- loading ----------

bool World::load(const std::string& dir) {
  buildTown(parseJson(readFile(dir + "/town.json")));
  JVal cities = parseJson(readFile(dir + "/cities.json"));
  for (auto& m : cities.at("maps").arr) buildTown(m);
  JVal iv = parseJson(readFile(dir + "/interiors.json"));
  for (auto& m : iv.at("interiors").arr) buildInterior(m);
  return true;
}

void World::bindWorld(const WorldGraph& graph) {
  for (auto& kv : maps_) {
    const CityDef* c = graph.cityForMap(kv.first);
    kv.second.cityId = c ? c->id : "";
    kv.second.regionId = kv.second.isInterior ? (c ? c->region + "_solo" : "solo")
                                              : (c ? c->region : "region_unknown");
    kv.second.instanceType = kv.second.isInterior ? "solo" : "shared";
  }
}

std::vector<std::string> World::mapIds() const {
  std::vector<std::string> ids;
  for (const auto& kv : maps_) ids.push_back(kv.first);
  return ids;
}

const TileMap& World::map(const std::string& id) const {
  static TileMap empty;
  auto it = maps_.find(id);
  return it == maps_.end() ? empty : it->second;
}

void World::buildTown(const JVal& j) {
  TileMap m;
  m.id = j.s("id"); m.name = j.s("name");
  if (m.id.empty()) return;
  m.ambient = j.s("ambient", "town");
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
    it.mapId = m.id;
    it.target = in.s("target");
    it.text = in.s("text");
    int tx = in.i("x"), ty = in.i("y");
    it.rect = {(float)tx * m.tile, (float)ty * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  for (auto& bd : m.buildings) {
    Interactable it;
    it.id = "door_" + bd.id; it.kind = "door"; it.label = bd.name;
    it.mapId = m.id; it.target = bd.target;
    it.rect = {(float)bd.doorX * m.tile, (float)bd.doorY * m.tile, (float)m.tile, (float)m.tile};
    m.interactables.push_back(it);
  }
  buildDecor(m);
  maps_[m.id] = m;
}

void World::buildInterior(const JVal& j) {
  TileMap m;
  m.id = j.s("id"); m.name = j.s("name");
  if (m.id.empty()) return;
  m.w = j.i("w", 14); m.h = j.i("h", 10);
  // Interiors get a larger tile so a 14x10 room fills the 960x600 canvas.
  m.tile = 56;
  m.isInterior = true;
  m.floor = j.s("floor", "wood");
  m.tiles.assign(m.w * m.h, Tile::Grass);
  for (int x = 0; x < m.w; x++) { m.tiles[x] = Tile::Wall; m.tiles[(m.h - 1) * m.w + x] = Tile::Wall; }
  for (int y = 0; y < m.h; y++) { m.tiles[y * m.w] = Tile::Wall; m.tiles[y * m.w + m.w - 1] = Tile::Wall; }
  const JVal& d = j.at("door");
  m.door = {d.s("label"), d.s("target"), d.i("x"), d.i("y")};
  m.tiles[m.door.y * m.w + m.door.x] = Tile::Path;
  if (m.door.y - 1 > 0) m.tiles[(m.door.y - 1) * m.w + m.door.x] = Tile::Path;
  for (auto& f : j.at("furniture").arr) {
    TileMap::Furn fu{f.s("label"), f.s("tile"), f.i("x"), f.i("y")};
    m.furniture.push_back(fu);
    m.tiles[fu.y * m.w + fu.x] = Tile::Wall;
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

  // Rugs and houseplants: decoration only, placed on free floor tiles.
  auto freeTile = [&](int x, int y) {
    return x > 1 && y > 1 && x < m.w - 2 && y < m.h - 2 &&
           m.at(x, y) != Tile::Wall && !(x == m.door.x && y == m.door.y);
  };
  if (freeTile(m.w / 2, m.h / 2)) m.decor.push_back({"rug", m.w / 2, m.h / 2, 0});
  if (freeTile(2, 2)) m.decor.push_back({"plant", 2, 2, 1});
  if (freeTile(m.w - 3, m.h - 4)) m.decor.push_back({"plant", m.w - 3, m.h - 4, 2});
  m.decor.push_back({"ceiling", m.w / 2, m.h / 2, 0});

  maps_[m.id] = m;
}

void World::buildDecor(TileMap& m) {
  if (m.isInterior || m.w == 0) return;
  auto nearOpen = [&](int x, int y) {
    return m.at(x, y) != Tile::Wall && m.at(x, y) != Tile::Water;
  };
  for (int y = 1; y < m.h - 1; y++) {
    for (int x = 1; x < m.w - 1; x++) {
      Tile t = m.at(x, y);
      if (!isPaved(t)) continue;
      // Keep interactables and door mats clear.
      bool occupied = false;
      for (const auto& it : m.interactables)
        if ((int)(it.rect.x / m.tile) == x && (int)(it.rect.y / m.tile) == y) { occupied = true; break; }
      if (occupied) continue;
      bool grassEdge = m.at(x - 1, y) == Tile::Grass || m.at(x + 1, y) == Tile::Grass ||
                       m.at(x, y - 1) == Tile::Grass || m.at(x, y + 1) == Tile::Grass;
      if (!grassEdge) continue;
      uint32_t h = h3(x, y, 200);
      if (h % 31 == 0 && nearOpen(x, y)) {
        m.decor.push_back({"lamp", x, y, (int)(h % 2)});
      } else if (h % 79 == 0) {
        m.decor.push_back({"bench", x, y, (int)(h % 2)});
      } else if (h % 67 == 0) {
        m.decor.push_back({"planter", x, y, (int)(h % 3)});
      } else if (t == Tile::Path && h % 53 == 0) {
        m.decor.push_back({"manhole", x, y, 0});
      }
    }
  }
}

Rect World::tileRect(const std::string& mapId, int tx, int ty) const {
  const TileMap& m = map(mapId);
  return {(float)tx * m.tile, (float)ty * m.tile, (float)m.tile, (float)m.tile};
}

// ---------- text ----------

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

// ---------- environment render ----------

void World::render(SDL_Renderer* ren, TTF_Font* font, const std::string& mapId,
                   const Vec2& cam, int vw, int vh, float dayT, double animT,
                   const std::vector<std::string>& hidden) {
  const TileMap& m = map(mapId);
  if (m.w == 0) return;
  int T = m.tile;
  int x0 = (int)(cam.x / T) - 2, x1 = (int)((cam.x + vw) / T) + 2;
  int y0 = (int)(cam.y / T) - 2, y1 = (int)((cam.y + vh) / T) + 3;

  Color grassA = m.isInterior
      ? (m.floor == "wood" ? C(158, 112, 74) : m.floor == "lab" ? C(196, 212, 218) : C(168, 152, 134))
      : C(86, 158, 94);
  Color grassB = m.isInterior ? shade(grassA, -12) : C(78, 148, 86);
  if (!m.isInterior) {
    if (m.cityId == "startup_city") { grassA = C(96, 166, 120); grassB = C(86, 152, 108); }
    else if (m.cityId == "systems_district") { grassA = C(98, 152, 136); grassB = C(86, 138, 124); }
    else if (m.cityId == "innovation_capital") { grassA = C(112, 158, 124); grassB = C(98, 144, 112); }
  }

  // Backdrop around small interior rooms so they read as a diorama.
  if (m.isInterior) {
    pr(ren, 0, 0, vw, vh, C(10, 13, 18));
  }

  for (int ty = y0; ty <= y1; ty++) {
    for (int tx = x0; tx <= x1; tx++) {
      int px = rnd(tx * T - cam.x), py = rnd(ty * T - cam.y);
      Tile t = m.at(tx, ty);
      if (m.isInterior) {
        if (t == Tile::Wall) drawWallTile(ren, m, tx, ty, px, py, T, animT, dayT);
        else drawFloorTile(ren, m, tx, ty, px, py, T, grassA, grassB);
        continue;
      }
      if (t == Tile::Wall) continue;
      if (t == Tile::Water) { drawWater(ren, m, tx, ty, px, py, T, animT); continue; }
      if (t == Tile::Path) { drawPath(ren, tx, ty, px, py, T); continue; }
      if (t == Tile::Plaza) { drawPlaza(ren, tx, ty, px, py, T); continue; }
      if (t == Tile::Flower) { drawFlowerTile(ren, tx, ty, px, py, T, grassA, grassB); continue; }
      if (t == Tile::Tree) { drawGrass(ren, m, tx, ty, px, py, T, grassA, grassB); continue; }
      drawGrass(ren, m, tx, ty, px, py, T, grassA, grassB);
    }
  }

  // Curbs and soft edges on pavement.
  if (!m.isInterior) {
    for (int ty = y0; ty <= y1; ty++) {
      for (int tx = x0; tx <= x1; tx++) {
        Tile t = m.at(tx, ty);
        if (!isPaved(t)) continue;
        int px = rnd(tx * T - cam.x), py = rnd(ty * T - cam.y);
        if (!isPaved(m.at(tx, ty - 1))) pr(ren, px, py, T, 2, C(226, 204, 164));
        if (!isPaved(m.at(tx, ty + 1))) pr(ren, px, py + T - 2, T, 2, C(164, 140, 108));
        if (!isPaved(m.at(tx - 1, ty))) pr(ren, px, py, 2, T, C(216, 194, 154));
        if (!isPaved(m.at(tx + 1, ty))) pr(ren, px + T - 2, py, 2, T, C(164, 140, 108));
      }
    }
  }

  // Trees sorted by base Y so canopies overlap correctly.
  if (!m.isInterior) {
    std::vector<std::pair<int, int>> trees;
    for (int ty = std::max(0, y0); ty <= std::min(m.h - 1, y1); ty++)
      for (int tx = std::max(0, x0); tx <= std::min(m.w - 1, x1); tx++)
        if (m.at(tx, ty) == Tile::Tree) trees.push_back({tx, ty});
    std::sort(trees.begin(), trees.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    for (auto& tr : trees) {
      int px = rnd(tr.first * T - cam.x), py = rnd(tr.second * T - cam.y);
      drawTree(ren, tr.first, tr.second, px, py, T);
    }
  }

  // Buildings.
  for (auto& b : m.buildings)
    drawBuilding(ren, font, m, b, (int)cam.x, (int)cam.y, animT, dayT);

  // Interior rugs first, then furniture and computers.
  for (auto& d : m.decor) {
    if (d.kind == "rug") {
      int px = rnd(d.x * T - cam.x), py = rnd(d.y * T - cam.y);
      pr(ren, px - T / 2, py - 8, T * 2, T - 18, C(126, 80, 72));
      pr(ren, px - T / 2 + 5, py - 3, T * 2 - 10, T - 28, C(172, 112, 96));
      pr(ren, px - T / 2 + 10, py + 2, T * 2 - 20, T - 38, C(142, 88, 78));
      for (int i = 0; i < 6; i++)
        pr(ren, px - T / 2 + 14 + i * 10, py + 4, 4, 2, C(208, 154, 124));
    } else if (d.kind == "plant") {
      int px = rnd(d.x * T - cam.x), py = rnd(d.y * T - cam.y);
      pr(ren, px + T / 2 - 8, py + T / 2 + 2, 16, 9, C(140, 84, 60));
      pr(ren, px + T / 2 - 7, py + T / 2, 14, 4, C(184, 116, 78));
      pcircle(ren, px + T / 2, py + T / 2 - 4, 11, C(48, 116, 64));
      pcircle(ren, px + T / 2 - 4, py + T / 2 - 7, 9, C(66, 144, 78));
      pcircle(ren, px + T / 2 + 3, py + T / 2 - 6, 7, C(90, 168, 94));
    }
  }
  {
    std::vector<std::pair<int, const TileMap::Furn*>> furns;
    for (auto& f : m.furniture) furns.push_back({f.y, &f});
    std::sort(furns.begin(), furns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : furns) {
      int px = rnd(f.second->x * T - cam.x), py = rnd(f.second->y * T - cam.y);
      drawFurniture(ren, m, *f.second, px, py, T, animT);
    }
  }
  for (auto& cp : m.computers) {
    int px = rnd(cp.x * T - cam.x), py = rnd(cp.y * T - cam.y);
    drawComputer(ren, m, cp, px, py, T, animT);
  }

  // Interaction objects, sorted by their base line.
  {
    std::vector<const Interactable*> props;
    for (auto& it : m.interactables) {
      if (it.kind == "door" || it.kind == "computer") continue;
      bool hide = false;
      for (auto& id : hidden) if (id == it.id) { hide = true; break; }
      if (hide) continue;
      props.push_back(&it);
    }
    std::sort(props.begin(), props.end(), [](const Interactable* a, const Interactable* b) {
      return a->rect.y < b->rect.y;
    });
    for (auto* it : props) {
      int px = rnd(it->rect.x - cam.x), py = rnd(it->rect.y - cam.y);
      drawInteractable(ren, font, m, *it, px, py, T, animT);
    }
  }

  // Decorative props: sorted with the rest of the street furniture.
  {
    std::vector<const Decor*> decs;
    for (auto& d : m.decor) {
      if (d.kind == "rug" || d.kind == "plant" || d.kind == "ceiling") continue;
      decs.push_back(&d);
    }
    std::sort(decs.begin(), decs.end(), [](const Decor* a, const Decor* b) {
      return a->y < b->y;
    });
    for (auto* d : decs) {
      int px = rnd(d->x * T - cam.x), py = rnd(d->y * T - cam.y);
      if (d->kind == "lamp") drawLamp(ren, px, py, T, d->variant);
      else if (d->kind == "bench") drawBench(ren, px, py, T);
      else if (d->kind == "planter") drawPlanter(ren, px, py, T);
      else if (d->kind == "manhole") {
        pcircle(ren, px + T / 2, py + T / 2, 10, C(124, 112, 96));
        pcircle(ren, px + T / 2, py + T / 2, 8, C(94, 84, 72));
        for (int i = 0; i < 4; i++)
          pr(ren, px + T / 2 - 5, py + T / 2 - 5 + i * 3, 10, 1, C(122, 110, 94));
      }
    }
  }

  // Interior exit mat.
  if (m.isInterior) {
    int px = rnd(m.door.x * T - cam.x), py = rnd(m.door.y * T - cam.y);
    float pulse = 0.65f + 0.35f * sinf((float)animT * 2.4f);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    pr(ren, px + 6, py + 6, T - 12, T - 12, C(52, 196, 178, (uint8_t)(70 + 50 * pulse)));
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    pr(ren, px + 10, py + 10, T - 20, T - 20, C(54, 150, 140));
    // Arrow pointing out.
    pr(ren, px + T / 2 - 6, py + T / 2 - 4, 12, 4, C(220, 246, 238));
    pr(ren, px + T / 2 - 3, py + T / 2 + 6, 6, 3, C(220, 246, 238));
  }
}

// ---------- characters ----------

// Set by Game::render before drawing characters (1.0 outdoors, ~1.5 indoors).
float g_charScale = 1.0f;

void World::drawCharacter(SDL_Renderer* ren, TTF_Font* font, float fx, float fy,
                          const Color& skin, const Color& shirt, const Color& pants,
                          const Color& hair, const std::string& name,
                          int facing, float phase, bool moving, bool isPlayer) {
  int x = rnd(fx);
  int y = rnd(fy);
  Color dark = C(22, 26, 34);
  Color skinShade = shade(skin, -24);
  Color shirtShade = shade(shirt, -30);
  Color pantShade = shade(pants, -28);
  Color boot = C(42, 38, 38);
  uint32_t h = 0;
  for (char c : name) h = h * 131 + (uint8_t)c;

  float swing = moving ? sinf(phase) : 0.0f;
  int bob = moving ? (int)(fabsf(sinf(phase)) * 2.0f) : 0;
  int by = y - bob;

  // The tile scale is inferred from the caller's canvas: Game passes a larger
  // scale through the sprite size below. Use a simple heuristic: interior
  // rooms use 56px tiles, exteriors 32. We detect by the name marker height
  // (kept in the caller through a static). Simpler: characters are drawn at
  // 1x outdoors and 1.55x indoors via `g_charScale`, set by Game::render.
  // (Declared at file scope for the render pass.)
  float s = g_charScale;
  auto R = [&](float v) { return rnd(v * s); };
  auto part = [&](int rx, int ry, int rw, int rh, Color c, bool outline = true) {
    if (outline) pr(ren, rx - R(1), ry - R(1), rw + R(2), rh + R(2), dark);
    pr(ren, rx, ry, rw, rh, c);
  };

  auto sx = [&](int v) { return x + R(v); };

  // Shadow.
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  pcircle(ren, x, y - R(1), R(8), C(8, 12, 16, 80));
  pr(ren, sx(-9), y - R(3), R(18), R(3), C(8, 12, 16, 70));
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

  // Legs and boots, with a tiny walk cycle.
  int lOff = moving ? (int)(swing * 2.0f) : 0;
  int rOff = moving ? (int)(-swing * 2.0f) : 0;
  part(sx(-7), by - R(12) + R(lOff), R(6), R(12), pants);
  part(sx(1), by - R(12) + R(rOff), R(6), R(12), pants);
  pr(ren, sx(-7), by - R(2) + R(lOff), R(6), R(3), boot);
  pr(ren, sx(1), by - R(2) + R(rOff), R(6), R(3), boot);

  // Torso.
  part(sx(-8), by - R(26), R(16), R(15), shirt);
  pr(ren, sx(-6), by - R(25), R(2), R(13), shade(shirt, 15));
  pr(ren, sx(4), by - R(25), R(3), R(12), shirtShade);
  // Collar.
  pr(ren, sx(-3), by - R(26), R(6), R(3), C(238, 232, 218));
  pr(ren, sx(-1), by - R(24), R(2), R(4), shirtShade);

  // Arms.
  part(sx(-11), by - R(24), R(4), R(12), shirt, false);
  part(sx(7), by - R(24), R(4), R(12), shirt, false);
  pr(ren, sx(-11), by - R(14), R(4), R(4), skin);
  pr(ren, sx(7), by - R(14), R(4), R(4), skin);

  // Head.
  part(sx(-7), by - R(38), R(14), R(12), skin);
  // Ears.
  pr(ren, sx(-8), by - R(33), R(2), R(3), skinShade);
  pr(ren, sx(6), by - R(33), R(2), R(3), skinShade);
  // Hair.
  Color hairShade = shade(hair, -28);
  int hairstyle = (h + (isPlayer ? 0 : 2)) % 4;
  if (facing == 3) {
    // Back of head: all hair.
    pr(ren, sx(-8), by - R(39), R(16), R(12), hair);
    pr(ren, sx(-7), by - R(38), R(14), R(3), shade(hair, 18));
    pr(ren, sx(-8), by - R(28), R(16), R(2), hairShade);
    if (hairstyle == 2) pcircle(ren, x, by - R(41), R(4), hair);
  } else {
    pr(ren, sx(-8), by - R(39), R(16), R(6), hair);
    pr(ren, sx(-7), by - R(38), R(14), R(2), shade(hair, 16));
    pr(ren, sx(-8), by - R(35), R(2), R(6), hair);
    pr(ren, sx(6), by - R(35), R(2), R(6), hair);
    if (hairstyle == 1) {  // long
      pr(ren, sx(-9), by - R(36), R(3), R(10), hair);
      pr(ren, sx(6), by - R(36), R(3), R(10), hair);
    } else if (hairstyle == 2) {  // bun
      pcircle(ren, x, by - R(42), R(4), hair);
      pr(ren, sx(-2), by - R(41), R(4), R(3), shade(hair, 18));
    } else if (hairstyle == 3) {  // fringe
      pr(ren, sx(-6), by - R(34), R(3), R(2), hair);
      pr(ren, sx(2), by - R(34), R(4), R(2), hair);
    }
    // Eyes and mouth by facing.
    if (facing == 0) {
      pr(ren, sx(-4), by - R(33), R(2), R(2), dark);
      pr(ren, sx(2), by - R(33), R(2), R(2), dark);
      pr(ren, sx(-1), by - R(29), R(2), R(1), shade(skin, -60));
    } else if (facing == 1) {
      pr(ren, sx(-5), by - R(33), R(2), R(2), dark);
      pr(ren, sx(-8), by - R(36), R(4), R(1), hair);
      pr(ren, sx(-4), by - R(29), R(2), R(1), shade(skin, -60));
    } else {
      pr(ren, sx(3), by - R(33), R(2), R(2), dark);
      pr(ren, sx(4), by - R(36), R(4), R(1), hair);
      pr(ren, sx(2), by - R(29), R(2), R(1), shade(skin, -60));
    }
  }

  // Player marker: a small teal chevron that bobs above the head.
  if (isPlayer) {
    float t = moving ? phase : (float)SDL_GetTicks() / 500.0f;
    int bob2 = (int)(sinf(t * 2.0f) * 2.0f);
    int mx = x, my = by - R(46) + bob2;
    pr(ren, mx - R(5), my, R(10), R(3), C(32, 54, 56));
    pr(ren, mx - R(4), my, R(8), R(2), C(74, 232, 204));
    pr(ren, mx - R(2), my + R(3), R(4), R(2), C(74, 232, 204));
    pr(ren, mx - R(1), my + R(5), R(2), R(1), C(74, 232, 204));
  }

  // NPC name tag.
  if (!isPlayer && font && !name.empty()) {
    std::string shortName = name;
    size_t sp = shortName.find(' ');
    if (sp != std::string::npos && sp > 0) shortName = shortName.substr(0, sp);
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, shortName.c_str(), &tw, &th);
    int bx = x - tw / 2 - 6;
    int byy = by - R(48) - th;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    pr(ren, bx, byy, tw + 12, th + 5, C(12, 17, 24, 185));
    pframe(ren, bx, byy, tw + 12, th + 5, C(58, 78, 82, 200), 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    drawText(ren, font, shortName, bx + 6, byy + 2, C(236, 240, 232, 255));
  }
}

// Set by Game::render before drawing characters (1.0 outdoors, 1.55 indoors).
// ---------- lighting ----------

void World::renderLighting(SDL_Renderer* ren, const std::string& mapId,
                           const Vec2& cam, int vw, int vh, float dayT, double animT) {
  const TileMap& m = map(mapId);
  if (m.w == 0) return;
  int T = m.tile;

  float dark = 0.0f;
  if (m.isInterior) {
    if (dayT < 0.25f || dayT > 0.82f) dark = 0.22f;
    else if (dayT < 0.32f) dark = 0.22f * (0.32f - dayT) / 0.07f;
    else if (dayT > 0.76f) dark = 0.22f * (dayT - 0.76f) / 0.06f;
  } else {
    if (dayT < 0.25f || dayT > 0.82f) dark = 0.50f;
    else if (dayT < 0.35f) dark = 0.50f * (0.35f - dayT) / 0.10f;
    else if (dayT > 0.72f) dark = 0.50f * (dayT - 0.72f) / 0.10f;
  }

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (dark > 0.001f) {
    // Night keeps a little blue; dusk leans warm.
    bool dusk = (dayT > 0.68f && dayT < 0.86f);
    Color tint = dusk ? C(28, 34, 72) : C(16, 24, 58);
    pr(ren, 0, 0, vw, vh, C(tint.r, tint.g, tint.b, (uint8_t)(dark * 255)));
    if (dusk) pr(ren, 0, 0, vw, vh, C(255, 148, 84, (uint8_t)(26 * (dark / 0.5f))));
  }

  // BBCode-style warm light pools. Additive so lamps read over the tint.
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
  auto glow = [&](int gx, int gy, int rad, Color c, int alpha) {
    // Three rings give a soft falloff without a shader.
    pcircle(ren, gx, gy, rad, C(c.r, c.g, c.b, (uint8_t)alpha));
    pcircle(ren, gx, gy, rad * 2 / 3, C(c.r, c.g, c.b, (uint8_t)(alpha * 1.5f > 255 ? 255 : alpha * 1.5f)));
    pcircle(ren, gx, gy, rad / 3, C(c.r, c.g, c.b, (uint8_t)(alpha * 2 > 255 ? 255 : alpha * 2)));
  };
  if (dark > 0.04f) {
    // Street lamps and interior ceiling lamps.
    for (auto& d : m.decor) {
      if (d.kind != "lamp" && d.kind != "ceiling") continue;
      int gx = rnd(d.x * T - cam.x + T / 2);
      int gy = rnd(d.y * T - cam.y + (d.kind == "lamp" ? 8 : T / 2));
      float flick = 0.92f + 0.08f * sinf((float)animT * 2.1f + d.x * 1.7f);
      glow(gx, gy, (m.isInterior ? 90 : 34), C(255, 196, 112), (int)((m.isInterior ? 24 : 42) * flick));
    }

    // Warm windows and door spill.
    for (auto& b : m.buildings) {
      int px = rnd(b.rect.x * T - cam.x);
      int py = rnd(b.rect.y * T - cam.y);
      int pw = rnd(b.rect.w * T);
      int ph = rnd(b.rect.h * T);
      int wy = py + ph - std::min(26, ph / 3) + 4;
      for (int wx = px + 10; wx < px + pw - 14; wx += 26) {
        if (((wx / 3) & 1) == 0) continue;
        pcircle(ren, wx + 5, wy + 4, 16, C(255, 190, 104, 26));
        pcircle(ren, wx + 5, wy + 4, 8, C(255, 202, 120, 36));
      }
      int dx = rnd(b.doorX * T - cam.x + T / 2);
      int dy = rnd(b.doorY * T - cam.y + T / 2);
      pcircle(ren, dx, dy, 30, C(255, 190, 112, 22));
    }

    // Computer screens and interior windows.
    if (m.isInterior) {
      for (auto& cp : m.computers) {
        int gx = rnd(cp.x * T - cam.x + T / 2);
        int gy = rnd(cp.y * T - cam.y + 10);
        pcircle(ren, gx, gy, 22, C(68, 220, 198, 20));
        pcircle(ren, gx, gy, 12, C(78, 232, 208, 30));
      }
      for (int ty = 0; ty <= 1; ty++)
        for (int tx = 1; tx < m.w - 1; tx++) {
          if (tx % 4 != 1 && tx % 4 != 2) continue;
          int gx = rnd(tx * T - cam.x + T / 2);
          int gy = rnd(ty * T - cam.y + 20);
          pcircle(ren, gx, gy, 26, C(255, 206, 134, 16));
        }
    }
  }

  // Player lantern: a soft radius so night navigation stays comfortable.
  (void)animT;

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

  // Night sky stars over exteriors.
  if (!m.isInterior && dark > 0.12f) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 80; i++) {
      int sx = (i * 173 + 37) % vw;
      int sy = (i * 97 + 13) % (vh / 2);
      float tw = 0.5f + 0.5f * sinf((float)animT * 1.3f + i * 0.9f);
      uint8_t a = (uint8_t)(dark * 255 * (0.30f + 0.55f * tw));
      pr(ren, sx, sy, (i % 7 == 0) ? 2 : 1, (i % 7 == 0) ? 2 : 1, C(224, 232, 255, a));
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
  }

  // Gentle vignette.
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < 8; i++) {
    uint8_t a = (uint8_t)(10 + i * 4);
    pr(ren, 0, i * 3, vw, 3, C(4, 7, 10, a));
    pr(ren, 0, vh - (i + 1) * 3, vw, 3, C(4, 7, 10, a));
    pr(ren, i * 3, 0, 3, vh, C(4, 7, 10, a));
    pr(ren, vw - (i + 1) * 3, 0, 3, vh, C(4, 7, 10, a));
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

}  // namespace dw
