// game.cpp (part 1) — init, content loading, save/load.
#include "game.h"
#include "bridge.h"
#include "util.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <emscripten.h>

namespace dw {

Game* g_game = nullptr;

static std::string readFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool Game::init(int vw, int vh) {
  vw_ = vw; vh_ = vh;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return false;
  if (TTF_Init() < 0) return false;
  win_ = SDL_CreateWindow("DEVWORLD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                          vw_, vh_, SDL_WINDOW_SHOWN);
  if (!win_) return false;
  ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren_) return false;
  font_ = TTF_OpenFont("assets/DejaVuSans.ttf", 14);
  // font_ may be null if the asset is missing; world draws text-free then.

  // Default keybinds (remappable in settings).
  keys_ = {{"up", SDLK_w}, {"down", SDLK_s}, {"left", SDLK_a}, {"right", SDLK_d},
           {"interact", SDLK_e}, {"map", SDLK_m}, {"quests", SDLK_j},
           {"inventory", SDLK_i}, {"close", SDLK_ESCAPE}};

  loadContent();
  loadOrCreate();

  // Start position: spawn tile -> px (center of tile).
  const TileMap& m = world_.map(curMap_);
  if (!hasSave_) {
    player_.pos = {m.spawn.x * m.tile + m.tile / 2, m.spawn.y * m.tile + m.tile / 2};
  }
  updateCamera();
  ui_update_hud();
  return true;
}

void Game::loadContent() {
  world_.load("content");
  loadNpcs(parseJson(readFile("content/npcs.json")));
  loadMissions(parseJson(readFile("content/missions.json")));
  loadShop(parseJson(readFile("content/shop.json")));
}

void Game::loadNpcs(const JVal& j) {
  for (auto& n : j.at("npcs").arr) {
    NpcDef d;
    d.id = n.s("id"); d.name = n.s("name"); d.job = n.s("job");
    d.rootNode = n.s("rootNode");
    const JVal& sh = n.at("shirt");
    d.shirt = {(uint8_t)sh.arr[0].num, (uint8_t)sh.arr[1].num, (uint8_t)sh.arr[2].num};
    const JVal& sk = n.at("skin");
    d.skin = {(uint8_t)sk.arr[0].num, (uint8_t)sk.arr[1].num, (uint8_t)sk.arr[2].num};
    for (auto& kv : n.at("nodes").obj) {
      DialogueNode node;
      node.id = kv.first;
      node.speaker = kv.second.s("speaker");
      node.text = kv.second.s("text");
      for (auto& o : kv.second.at("options").arr) {
        DialogueOption op;
        op.text = o.s("text"); op.next = o.s("next");
        op.requires = o.s("requires"); op.sets = o.s("sets");
        node.options.push_back(op);
      }
      d.nodes[kv.first] = node;
    }
    for (auto& s : n.at("schedule").arr) {
      NpcScheduleEntry e;
      e.fromHour = s.i("fromHour"); e.toHour = s.i("toHour");
      e.mapId = s.s("mapId"); e.x = (float)s.n("x"); e.y = (float)s.n("y");
      d.schedule.push_back(e);
    }
    npcs_.push_back(d);
  }
}

void Game::loadMissions(const JVal& j) {
  for (auto& m : j.at("missions").arr) {
    MissionDef d;
    d.id = m.s("id"); d.title = m.s("title"); d.giver = m.s("giver");
    d.briefing = m.s("briefing"); d.language = m.s("language");
    d.file = m.s("file"); d.starter = m.s("starter");
    d.xpReward = m.n("xpReward"); d.creditReward = m.n("creditReward");
    for (auto& r : m.at("requires").arr) d.requires.push_back(r.str);
    for (auto& t : m.at("tests").arr) {
      TestCase tc;
      tc.name = t.s("name"); tc.tstdin = t.s("stdin");
      tc.expected = t.s("expected"); tc.hidden = t.boolean("hidden");
      d.tests.push_back(tc);
    }
    missions_[d.id] = d;
    // First mission available immediately; rest unlock via `requires`.
    MissionProgress pr;
    pr.state = d.id == "m01" ? MissionState::Available : MissionState::Locked;
    player_.missions[d.id] = pr;
  }
}

void Game::loadShop(const JVal& j) {
  for (auto& it : j.at("items").arr) {
    ItemDef d;
    d.id = it.s("id"); d.name = it.s("name"); d.desc = it.s("desc");
    d.price = it.n("price"); d.slot = it.s("slot");
    shopItems_.push_back(d);
  }
}

// ---------- persistence ----------
// Save shape: devworld.save.v1 (localStorage). Everything player-visible.
// [PHASE2] valuable state (ledger, progression, unlocks) moves server-side;
// the client will keep only a cache + signed snapshot. Never trust the client
// for economy in the real design.

// --- localStorage bridge (EM_JS must live at file scope, not in a function)
EM_JS(void, dw_save, (const char* json), {
  try { localStorage.setItem('devworld.save.v1', UTF8ToString(json)); } catch (e) {}
});

void Game::saveNow() {
  std::string s = "{";
  s += "\"name\":" + json_escape(player_.name) + ",";
  s += "\"displayName\":" + json_escape(player_.displayName) + ",";
  s += "\"skin\":[" + std::to_string(player_.skin.r) + "," + std::to_string(player_.skin.g) + "," +
       std::to_string(player_.skin.b) + "],";
  s += "\"shirt\":[" + std::to_string(player_.shirt.r) + "," + std::to_string(player_.shirt.g) + "," +
       std::to_string(player_.shirt.b) + "],";
  s += "\"pants\":[" + std::to_string(player_.pants.r) + "," + std::to_string(player_.pants.g) + "," +
       std::to_string(player_.pants.b) + "],";
  s += "\"x\":" + std::to_string(player_.pos.x) + ",\"y\":" + std::to_string(player_.pos.y) + ",";
  s += "\"map\":" + json_escape(curMap_) + ",";
  s += "\"level\":" + std::to_string(player_.levels.level) + ",";
  s += "\"xp\":" + std::to_string(player_.levels.xp) + ",";
  s += "\"reputation\":" + std::to_string(player_.reputation) + ",";
  s += "\"dayT\":" + std::to_string(dayT_) + ",";
  s += "\"inventory\":{";
  bool first = true;
  for (auto& kv : player_.inventory) {
    if (!first) s += ",";
    first = false;
    s += json_escape(kv.first) + ":" + std::to_string(kv.second);
  }
  s += "},\"missions\":{";
  first = true;
  for (auto& kv : player_.missions) {
    if (!first) s += ",";
    first = false;
    s += json_escape(kv.first) + ":{\"state\":" +
         std::to_string((int)kv.second.state) + ",\"attempts\":" +
         std::to_string(kv.second.attempts) + "}";
  }
  s += "},\"ledger\":[";
  first = true;
  for (auto& r : player_.ledger) {
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(r.id) + ",\"ts\":" + json_escape(r.ts) +
         ",\"kind\":" + json_escape(r.kind) + ",\"memo\":" + json_escape(r.memo) +
         ",\"delta\":" + std::to_string(r.delta) + "}";
  }
  s += "],\"achievements\":[";
  first = true;
  for (auto& a : player_.achievements) {
    if (!first) s += ",";
    first = false;
    s += json_escape(a);
  }
  s += "],\"flags\":{";
  first = true;
  for (auto& kv : player_.flags) {
    if (!first) s += ",";
    first = false;
    s += json_escape(kv.first) + ":" + json_escape(kv.second);
  }
  s += "}}";
  dw_save(s.c_str());
}

void Game::loadOrCreate() {
  std::string s = load_save();
  if (s.empty()) {
    hasSave_ = false;
    // Character creation panel (DOM). Game starts after uiEvent "char_done".
    ui_show("charcreate");
    uiOpen_ = true;
    return;
  }
  hasSave_ = true;
  try {
    JVal j = parseJson(s);
    player_.name = j.s("name", "Dev");
    player_.displayName = j.s("displayName", "Dev");
    auto col = [&](const std::string& k, Color d) {
      const JVal& a = j.at(k);
      if (a.type == JVal::ARR && a.arr.size() >= 3)
        return Color{(uint8_t)a.arr[0].num, (uint8_t)a.arr[1].num, (uint8_t)a.arr[2].num};
      return d;
    };
    player_.skin = col("skin", player_.skin);
    player_.shirt = col("shirt", player_.shirt);
    player_.pants = col("pants", player_.pants);
    player_.pos = {(float)j.n("x"), (float)j.n("y")};
    curMap_ = j.s("map", "town");
    player_.levels.level = j.i("level", 1);
    player_.levels.xp = (int64_t)j.n("xp", 0);
    player_.reputation = (int64_t)j.n("reputation", 0);
    dayT_ = (float)j.n("dayT", 0.72);
    for (auto& kv : j.at("inventory").obj) player_.inventory[kv.first] = (int)kv.second.num;
    for (auto& kv : j.at("missions").obj) {
      MissionProgress pr;
      pr.state = (MissionState)kv.second.i("state", 0);
      pr.attempts = kv.second.i("attempts", 0);
      player_.missions[kv.first] = pr;
    }
    for (auto& r : j.at("ledger").arr) {
      LedgerRow row;
      row.id = r.s("id"); row.ts = r.s("ts"); row.kind = r.s("kind");
      row.memo = r.s("memo"); row.delta = (int64_t)r.n("delta");
      player_.ledger.push_back(row);
    }
    for (auto& a : j.at("achievements").arr)
      if (a.type == JVal::STR) player_.achievements.push_back(a.str);
    for (auto& kv : j.at("flags").obj) player_.flags[kv.first] = kv.second.str;
  } catch (...) {
    // Corrupt save -> fresh start (never crash on bad data).
    hasSave_ = false;
    ui_show("charcreate");
    uiOpen_ = true;
  }
}

}  // namespace dw
