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
#include "world_model.h"

namespace dw {

enum class Tile { Grass, Path, Plaza, Water, Tree, Flower, Wall };

struct Building {
  std::string id, name;
  Rect rect;  // tiles
  Color wall, roof;
  int doorX = 0, doorY = 0;  // tile of the door mat (outside)
  std::string target;        // interior map id
};

// Small decorative props placed by the world builder (street lamps, benches,
// planters). Pure decoration: never collides, never changes the sim.
struct Decor {
  std::string kind;  // "lamp" | "bench" | "planter" | "manhole" | "crate"
  int x = 0, y = 0;
  int variant = 0;
};

struct TileMap {
  std::string id, name;
  int w = 0, h = 0, tile = 32;
  std::vector<Tile> tiles;   // w*h
  std::vector<Building> buildings;
  std::vector<Interactable> interactables;
  std::vector<Decor> decor;
  Vec2 spawn{0, 0};          // tiles
  // world ownership (multiplayer-ready metadata; see docs design §5)
  std::string cityId;        // "starter_town" | "startup_city" | ...
  std::string regionId;      // server shard/region id
  std::string instanceType = "shared";  // "shared" exterior | "solo" interior
  std::string ambient = "town";         // sound/light mood
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
  // Attach city/region/instance metadata after the graph is loaded.
  void bindWorld(const WorldGraph& graph);
  const TileMap& map(const std::string& id) const;
  bool has(const std::string& id) const { return maps_.count(id) == 1; }
  std::vector<std::string> mapIds() const;

  // Render the given map centered on camera (world px).
  // `hidden` lists interactable ids the player has not discovered yet (for
  // example sealed technology hatches); they are not drawn at all.
  void render(SDL_Renderer* ren, TTF_Font* font, const std::string& mapId,
              const Vec2& cam, int vw, int vh, float dayT, double animT,
              const std::vector<std::string>& hidden = {});

  // Light and atmosphere pass. Drawn AFTER characters so dusk/night grading
  // touches the whole scene, then lamp light and window spill are added back.
  void renderLighting(SDL_Renderer* ren, const std::string& mapId,
                      const Vec2& cam, int vw, int vh, float dayT, double animT);

  // Pixel-art character sprite (player and NPCs). (x,y) is the feet point.
  void drawCharacter(SDL_Renderer* ren, TTF_Font* font, float x, float y,
                     const Color& skin, const Color& shirt, const Color& pants,
                     const Color& hair, const std::string& name,
                     int facing, float phase, bool moving, bool isPlayer);

  // Pixel-rect of a tile, for interaction checks.
  Rect tileRect(const std::string& mapId, int tx, int ty) const;

 private:
  std::map<std::string, TileMap> maps_;
  void buildTown(const JVal& j);
  void buildInterior(const JVal& j);
  void buildDecor(TileMap& m);
  void drawText(SDL_Renderer* ren, TTF_Font* font, const std::string& text,
                int x, int y, Color c);
};

// Sprite scale used by drawCharacter. Game::render sets this per map:
// 1.0 for 32px exteriors, ~1.5 for the larger interior tiles.
extern float g_charScale;

}  // namespace dw
