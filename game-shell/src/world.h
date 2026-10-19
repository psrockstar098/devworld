#pragma once
// world.h — tile maps, buildings, collision, and world rendering.
// Phase 1: Starter Town exterior + 6 small interiors. Maps are data
// (content/*.json); this module builds collision grids and draws them.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <map>
#include "types.h"
#include "json.h"

namespace dw {

enum class Tile { Grass, Path, Plaza, Water, Tree, Flower, Wall };

struct Building {
  std::string id, name;
  Rect rect;  // tiles
  Color wall, roof;
  int doorX = 0, doorY = 0;  // tile of the door mat (outside)
  std::string target;        // interior map id
};

struct TileMap {
  std::string id, name;
  int w = 0, h = 0, tile = 32;
  std::vector<Tile> tiles;   // w*h
  std::vector<Building> buildings;
  std::vector<Interactable> interactables;
  Vec2 spawn{0, 0};          // tiles
  // interior extras
  std::string floor = "grass";
  struct Furn { std::string label, tile; int x = 0, y = 0; };
  std::vector<Furn> furniture;
  struct Comp { int x = 0, y = 0; };
  std::vector<Comp> computers;
  struct Door { std::string label, target; int x = 0, y = 0; };
  Door door{"", "", 0, 0};
  bool isInterior = false;

  Tile at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return Tile::Wall;
    return tiles[y * w + x];
  }
  bool solid(int x, int y) const {
    Tile t = at(x, y);
    if (t == Tile::Wall || t == Tile::Water || t == Tile::Tree) return true;
    return false;
  }
};

class World {
 public:
  bool load(const std::string& contentDir);
  const TileMap& map(const std::string& id) const;

  // Render the given map centered on camera (world px), with day/night tint.
  void render(SDL_Renderer* ren, TTF_Font* font, const std::string& mapId,
              const Vec2& cam, int vw, int vh, float dayT, double animT);

  // Pixel-rect of a tile, for interaction checks.
  Rect tileRect(const std::string& mapId, int tx, int ty) const;

 private:
  std::map<std::string, TileMap> maps_;
  void buildTown(const JVal& j);
  void buildInterior(const JVal& j);
  void drawPlayerNpc(SDL_Renderer* ren, float x, float y, const Color& skin,
                     const Color& shirt, const Color& pants, const std::string& name,
                     TTF_Font* font, bool facingLeft, float bob);
  void drawText(SDL_Renderer* ren, TTF_Font* font, const std::string& text,
                int x, int y, Color c);
};

}  // namespace dw
