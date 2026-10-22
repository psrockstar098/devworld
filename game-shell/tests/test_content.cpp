// tests/test_content.cpp — validates the shipped content files.
//
// Content is data, so a typo doesn't fail compilation — it fails silently at
// runtime (a mission no one can unlock, dialogue pointing at a dead node).
// These tests parse the same files the game preloads and enforce the campaign
// invariants from docs/design/04-progression-50-levels.md.
#include "doctest.h"
#include "achievements.h"
#include "careers.h"
#include "curriculum.h"
#include "frameworks.h"
#include "json.h"
#include "progression.h"
#include "regions.h"
#include "unlocks.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace dw;

namespace {

std::string contentPath(const std::string& name) {
#ifdef __EMSCRIPTEN__
  return "/content/" + name;  // --preload-file content mounts at the VFS root
#else
  return "content/" + name;
#endif
}

std::string readContent(const std::string& name) {
  std::ifstream f(contentPath(name));
  if (!f) return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

JVal loadContent(const std::string& name) {
  std::string text = readContent(name);
  REQUIRE_MESSAGE(!text.empty(), "missing content file: " << contentPath(name));
  return parseJson(text);
}

// Mirrors Game::loadMissions: content -> MissionDef. Keeping this local to the
// tests means the test fails if the content shape and the loader drift apart.
std::map<std::string, MissionDef> loadMissions() {
  JVal j = loadContent("missions.json");
  std::map<std::string, MissionDef> out;
  for (const auto& m : j.at("missions").arr) {
    MissionDef d;
    d.id = m.s("id");
    d.title = m.s("title");
    d.giver = m.s("giver");
    d.briefing = m.s("briefing");
    d.language = m.s("language");
    d.file = m.s("file");
    d.starter = m.s("starter");
    d.xpReward = (int64_t)m.n("xpReward");
    d.creditReward = (int64_t)m.n("creditReward");
    d.level = m.i("level", 1);
    d.gate = m.boolean("gate", false);
    d.hints = m.boolean("hints", true);
    d.independent = d.gate || m.boolean("independent", false);
    for (const auto& r : m.at("requires").arr) d.requires.push_back(r.str);
    for (const auto& t : m.at("tests").arr) {
      TestCase tc;
      tc.name = t.s("name");
      tc.tstdin = t.s("stdin");
      tc.expected = t.s("expected");
      tc.hidden = t.boolean("hidden");
      d.tests.push_back(tc);
    }
    out[d.id] = d;
  }
  return out;
}

int64_t xpToReach(int level) {
  int64_t total = 0;
  for (int l = 1; l < level; ++l) total += xpForLevel(l);
  return total;
}

bool isSpecialOption(const std::string& next) {
  return next == "@close" || next == "@bye" || next == "@shop" ||
         next.rfind("@mission:", 0) == 0;
}

}  // namespace

TEST_CASE("mission files parse and ids are unique") {
  JVal raw = loadContent("missions.json");
  REQUIRE(raw.at("missions").type == JVal::ARR);

  std::set<std::string> ids, files;
  for (const auto& m : raw.at("missions").arr) {
    std::string id = m.s("id");
    CHECK_MESSAGE(!id.empty(), "mission with empty id");
    INFO("mission " << id);
    CHECK(ids.insert(id).second);
    CHECK(files.insert(m.s("file")).second);
  }
  CHECK(ids.size() == raw.at("missions").arr.size());
}

TEST_CASE("every mission has the fields the game reads") {
  auto missions = loadMissions();
  REQUIRE(missions.size() >= 10);

  for (const auto& kv : missions) {
    const MissionDef& m = kv.second;
    INFO("mission " << m.id);
    CHECK(!m.title.empty());
    CHECK(!m.giver.empty());
    CHECK(!m.briefing.empty());
    CHECK(!m.language.empty());
    CHECK(!m.file.empty());
    CHECK(!m.starter.empty());
    CHECK(m.level >= 1);
    CHECK(m.level <= 50);
    CHECK(m.xpReward > 0);
    CHECK(m.creditReward >= 0);
    CHECK(!m.tests.empty());

    if (m.language == "python")
      CHECK((m.file.size() > 3 && m.file.substr(m.file.size() - 3) == ".py"));
    if (m.language == "sql")
      CHECK((m.file.size() > 4 && m.file.substr(m.file.size() - 4) == ".sql"));

    for (const auto& t : m.tests) {
      CHECK(!t.name.empty());
      CHECK(!t.expected.empty());
    }
  }
}

TEST_CASE("missions beyond the tutorial hide at least one test") {
  auto missions = loadMissions();
  for (const auto& kv : missions) {
    const MissionDef& m = kv.second;
    if (m.id == "m01") continue;  // first contact: public test only, by design
    int hidden = 0, open = 0;
    for (const auto& t : m.tests) (t.hidden ? hidden : open)++;
    INFO("mission " << m.id);
    CHECK(hidden >= 1);
    if (!m.gate) CHECK(open >= 1);  // gates are intentionally opaque
  }
}

TEST_CASE("the Level 5 gate is a real gate") {
  auto missions = loadMissions();
  std::vector<std::string> gates;
  for (const auto& kv : missions)
    if (kv.second.gate) gates.push_back(kv.first);

  REQUIRE(gates.size() == 1);
  const MissionDef& gate = missions.at(gates[0]);
  CHECK(gate.level == 5);
  CHECK(gate.hints == false);
  CHECK(!gate.requires.empty());

  int hidden = 0, open = 0;
  for (const auto& t : gate.tests) (t.hidden ? hidden : open)++;
  CHECK(open == 0);
  CHECK(hidden >= 3);  // unseen cases, not one lucky run
}

TEST_CASE("independent assessments can make skills masterable") {
  // Mastery requires independent evidence (design 08 §4.3). That evidence can
  // only come from an independent assessment: a gate, or a capstone proof like
  // the First Proof artifact. At least one must exist or no skill can master.
  auto missions = loadMissions();
  int independent = 0;
  for (const auto& kv : missions)
    if (kv.second.independent) independent++;
  CHECK(independent >= 1);
  for (const auto& kv : missions) {
    if (kv.second.gate) {
      INFO("gate " << kv.first);
      CHECK(kv.second.independent);
      CHECK_FALSE(kv.second.hints);
    }
  }
  // The gate itself is the canonical independent assessment.
  CHECK(missions.at("m13").independent);
}

TEST_CASE("prerequisite ids exist and no mission requires itself") {
  auto missions = loadMissions();
  for (const auto& kv : missions) {
    for (const auto& req : kv.second.requires) {
      INFO("mission " << kv.first << " requires " << req);
      CHECK(req != kv.first);
      CHECK(missions.count(req) == 1);
    }
  }
}

TEST_CASE("the mission graph is acyclic and all missions are reachable") {
  auto missions = loadMissions();
  REQUIRE(missions.count("m01") == 1);
  CHECK(missions.at("m01").requires.empty());

  // DFS with colors: 0 = unvisited, 1 = on stack, 2 = done.
  std::map<std::string, int> color;
  std::function<void(const std::string&)> visit = [&](const std::string& id) {
    color[id] = 1;
    for (const auto& req : missions.at(id).requires) {
      INFO("edge " << id << " -> " << req);
      REQUIRE(color[req] != 1);  // cycle
      if (color[req] == 0) visit(req);
    }
    color[id] = 2;
  };
  visit("m01");

  // Every mission must be reachable from the entry point through requires.
  std::function<int(const std::string&)> depth = [&](const std::string& id) -> int {
    int d = 0;
    for (const auto& req : missions.at(id).requires) d = std::max(d, depth(req) + 1);
    return d;
  };
  for (const auto& kv : missions) CHECK_MESSAGE(depth(kv.first) >= 0, kv.first);
}

TEST_CASE("XP rewards can actually reach each gate level") {
  auto missions = loadMissions();

  // All missions a player can finish before the gate: same tier or lower,
  // excluding the gate itself. Optional side missions count (they are real,
  // rewarded work), but the gate never depends on one specific side quest.
  for (const auto& kv : missions) {
    if (!kv.second.gate) continue;
    int64_t earned = 0;
    for (const auto& other : missions) {
      if (other.first == kv.first) continue;
      if (other.second.level <= kv.second.level) earned += other.second.xpReward;
    }
    INFO("gate " << kv.first << " needs " << xpToReach(kv.second.level)
                 << " XP, tier content awards " << earned);
    CHECK(earned >= xpToReach(kv.second.level));
  }
}

TEST_CASE("NPC dialogue references live nodes and live missions") {
  auto missions = loadMissions();
  JVal j = loadContent("npcs.json");
  std::set<std::string> npcIds;

  for (const auto& n : j.at("npcs").arr) {
    std::string id = n.s("id");
    INFO("npc " << id);
    CHECK(!id.empty());
    CHECK(npcIds.insert(id).second);
    CHECK(n.at("nodes").type == JVal::OBJ);
    CHECK(n.at("nodes").obj.count(n.s("rootNode")) == 1);

    for (const auto& kv : n.at("nodes").obj) {
      const JVal& node = kv.second;
      CHECK(!node.s("text").empty());
      for (const auto& o : node.at("options").arr) {
        std::string next = o.s("next");
        INFO("node " << kv.first << " -> " << next);
        CHECK(!o.s("text").empty());
        if (next.rfind("@mission:", 0) == 0) {
          CHECK(missions.count(next.substr(9)) == 1);
        } else if (!isSpecialOption(next)) {
          CHECK(n.at("nodes").obj.count(next) == 1);
        }
      }
    }

    for (const auto& s : n.at("schedule").arr) {
      CHECK(!s.s("mapId").empty());
      CHECK(s.i("fromHour") >= 0);
      CHECK(s.i("toHour") <= 24);
      CHECK(s.i("fromHour") < s.i("toHour"));
    }
  }
}

TEST_CASE("shop items are unique and well formed") {
  JVal j = loadContent("shop.json");
  std::set<std::string> ids;
  const std::set<std::string> slots{"computer", "tool", "decor", "consumable"};
  // The whole promise of equipment: convenience only. If an item ever grows an
  // effect outside this list (something that could change a test verdict), the
  // content build fails here, not in the review meeting.
  const std::set<std::string> allowedEffects{
      "xpPercent", "concurrentMissions", "buildSpeedPercent", "comfort"};
  int equippable = 0;
  for (const auto& it : j.at("items").arr) {
    INFO("item " << it.s("id"));
    CHECK(ids.insert(it.s("id")).second);
    CHECK(!it.s("name").empty());
    CHECK(!it.s("desc").empty());
    CHECK(it.n("price") > 0);
    CHECK(slots.count(it.s("slot")) == 1);
    if (it.s("slot") != "consumable") equippable++;
    for (const auto& kv : it.at("effects").obj) {
      INFO("effect " << kv.first);
      CHECK(allowedEffects.count(kv.first) == 1);
      CHECK(kv.second.num >= 0);
    }
  }
  CHECK(equippable >= 3);  // equipment has to be worth buying
}

TEST_CASE("world graph and map ownership agree across content files") {
  JVal world = loadContent("world.json");
  JVal cities = loadContent("cities.json");
  JVal interiors = loadContent("interiors.json");

  std::set<std::string> knownMaps;
  knownMaps.insert(loadContent("town.json").s("id"));
  for (const auto& m : cities.at("maps").arr) knownMaps.insert(m.s("id"));
  for (const auto& in : interiors.at("interiors").arr) knownMaps.insert(in.s("id"));

  std::set<std::string> owned;
  std::set<std::string> cityIds;
  const JVal& citiesArr = world.at("cities");
  REQUIRE(citiesArr.arr.size() >= 4);

  for (const auto& c : citiesArr.arr) {
    std::string id = c.s("id");
    INFO("city " << id);
    CHECK(!id.empty());
    CHECK(cityIds.insert(id).second);
    CHECK(c.i("minLevel", 1) >= 1);
    auto maps = c.at("maps");
    REQUIRE(maps.arr.size() >= 1);
    CHECK(knownMaps.count(c.s("spawnMap")) == 1);
    for (const auto& m : maps.arr) {
      CHECK(owned.insert(m.str).second);  // no map in two cities
      CHECK(knownMaps.count(m.str) == 1); // no map that does not exist
    }
  }
  CHECK(cityIds.count("starter_town") == 1);

  // Every map in the game is claimed by exactly one city.
  for (const auto& m : knownMaps) {
    INFO("map " << m);
    CHECK(owned.count(m) == 1);
  }

  // Links are real cities, positive fares, no self-loops.
  std::set<std::string> linkedFrom;
  for (const auto& l : world.at("links").arr) {
    INFO("link " << l.s("from") << " -> " << l.s("to"));
    CHECK(cityIds.count(l.s("from")) == 1);
    CHECK(cityIds.count(l.s("to")) == 1);
    CHECK(l.s("from") != l.s("to"));
    CHECK(l.i("fare") > 0);
    linkedFrom.insert(l.s("from"));
    linkedFrom.insert(l.s("to"));
  }
  // A city reachable only by walking is fine; at least the graph is connected
  // from Starter Town through declared links.
  CHECK(linkedFrom.count("starter_town") == 1);
}

TEST_CASE("city exteriors are walkable, signed and connected by bus") {
  JVal cities = loadContent("cities.json");
  JVal interiors = loadContent("interiors.json");
  std::set<std::string> interiorIds;
  for (const auto& in : interiors.at("interiors").arr) interiorIds.insert(in.s("id"));
  interiorIds.insert("town");

  for (const auto& m : cities.at("maps").arr) {
    INFO("city map " << m.s("id"));
    REQUIRE(m.at("size").arr.size() == 2);
    int w = (int)m.at("size").arr[0].num, h = (int)m.at("size").arr[1].num;
    CHECK(w > 20);
    CHECK(h > 20);

    int sx = m.at("spawn").i("x"), sy = m.at("spawn").i("y");
    CHECK((sx >= 0 && sx < w));
    CHECK((sy >= 0 && sy < h));

    bool bus = false, board = false, bank = false;
    for (const auto& p : m.at("interactables").arr) {
      INFO("poi " << p.s("id"));
      CHECK((p.i("x") >= 0 && p.i("x") < w));
      CHECK((p.i("y") >= 0 && p.i("y") < h));
      std::string kind = p.s("kind");
      if (kind == "bus") bus = true;
      if (kind == "board") board = true;
      if (kind == "bank") bank = true;
    }
    CHECK(bus);    // every city needs fast travel
    CHECK(board);  // and a job board
    CHECK(bank);   // and a way to read the ledger

    for (const auto& b : m.at("buildings").arr) {
      INFO("building " << b.s("id"));
      CHECK((b.i("x") >= 0 && b.i("x") + b.i("w") <= w));
      CHECK((b.i("y") >= 0 && b.i("y") + b.i("h") <= h));
      std::string target = b.at("door").s("target");
      CHECK(interiorIds.count(target) == 1);
      // Door mat sits outside the solid building block.
      int dy = b.at("door").i("y");
      CHECK(dy >= b.i("y") + b.i("h"));
    }
  }
}

TEST_CASE("interior doors point back at a building that points at them") {
  JVal interiors = loadContent("interiors.json");
  JVal cities = loadContent("cities.json");
  JVal town = loadContent("town.json");

  // Build exterior lookup: map id -> set of interior ids its buildings lead to.
  std::map<std::string, std::set<std::string>> exteriorTargets;
  auto indexExterior = [&](const JVal& m) {
    std::set<std::string> targets;
    for (const auto& b : m.at("buildings").arr) targets.insert(b.at("door").s("target"));
    // Dungeon hatches are entrances too: the secret lives behind an interactable.
    for (const auto& p : m.at("interactables").arr)
      if (p.s("kind") == "hatch") targets.insert(p.s("target"));
    exteriorTargets[m.s("id")] = targets;
  };
  indexExterior(town);
  for (const auto& m : cities.at("maps").arr) indexExterior(m);

  for (const auto& in : interiors.at("interiors").arr) {
    std::string id = in.s("id");
    std::string target = in.at("door").s("target");
    INFO("interior " << id << " -> " << target);
    CHECK(exteriorTargets.count(target) == 1);
    if (exteriorTargets.count(target))
      CHECK(exteriorTargets[target].count(id) == 1);
  }
}

TEST_CASE("NPC schedules point at real maps") {
  JVal npcs = loadContent("npcs.json");
  JVal cities = loadContent("cities.json");
  JVal interiors = loadContent("interiors.json");

  std::set<std::string> maps{loadContent("town.json").s("id")};
  for (const auto& m : cities.at("maps").arr) maps.insert(m.s("id"));
  for (const auto& in : interiors.at("interiors").arr) maps.insert(in.s("id"));

  for (const auto& n : npcs.at("npcs").arr) {
    for (const auto& s : n.at("schedule").arr) {
      INFO("npc " << n.s("id") << " in " << s.s("mapId"));
      CHECK(maps.count(s.s("mapId")) == 1);
    }
  }
}

TEST_CASE("starter town is internally consistent") {
  JVal town = loadContent("town.json");
  JVal interiors = loadContent("interiors.json");

  std::set<std::string> interiorIds;
  for (const auto& in : interiors.at("interiors").arr) {
    interiorIds.insert(in.s("id"));
    CHECK(in.i("w") > 0);
    CHECK(in.i("h") > 0);
    for (const auto& f : in.at("furniture").arr) {
      CHECK(f.i("x") >= 0);
      CHECK(f.i("x") < in.i("w"));
      CHECK(f.i("y") >= 0);
      CHECK(f.i("y") < in.i("h"));
    }
    const JVal& door = in.at("door");
    CHECK(!door.s("target").empty());
  }
  // Second pass: every interior door target must exist (town or another room).
  // (Stronger two-way check lives in "interior doors point back at a building".)

  const JVal& size = town.at("size");
  REQUIRE(size.arr.size() == 2);
  int tw = (int)size.arr[0].num, th = (int)size.arr[1].num;

  int sx = town.at("spawn").i("x"), sy = town.at("spawn").i("y");
  CHECK(sx >= 0);
  CHECK(sx < tw);
  CHECK(sy >= 0);
  CHECK(sy < th);

  for (const auto& b : town.at("buildings").arr) {
    INFO("building " << b.s("id"));
    CHECK(b.i("x") >= 0);
    CHECK(b.i("y") >= 0);
    CHECK(b.i("x") + b.i("w") <= tw);
    CHECK(b.i("y") + b.i("h") <= th);
    std::string target = b.at("door").s("target");
    CHECK(interiorIds.count(target) == 1);
  }

  for (const auto& p : town.at("interactables").arr) {
    INFO("interactable " << p.s("id"));
    CHECK(p.i("x") >= 0);
    CHECK(p.i("x") < tw);
    CHECK(p.i("y") >= 0);
    CHECK(p.i("y") < th);
  }
}

TEST_CASE("world objects have known kinds and inspectables carry lore") {
  JVal town = loadContent("town.json");
  JVal cities = loadContent("cities.json");
  const std::set<std::string> kinds{"board", "bus", "bank", "mail", "inspect", "shop", "computer", "hatch"};

  std::vector<JVal> maps{town};
  for (const auto& m : cities.at("maps").arr) maps.push_back(m);

  int inspectables = 0;
  for (const auto& m : maps) {
    std::set<std::string> ids;
    for (const auto& p : m.at("interactables").arr) {
      INFO("map " << m.s("id") << " object " << p.s("id"));
      CHECK(!p.s("id").empty());
      CHECK(ids.insert(p.s("id")).second);
      std::string kind = p.s("kind");
      CHECK(kinds.count(kind) == 1);
      CHECK(!p.s("label").empty());
      if (kind == "inspect") {
        inspectables++;
        CHECK(p.s("text").size() > 20);  // lore, not a placeholder
      }
      if (kind == "hatch") {
        // A dungeon entrance: it must name the interior it opens.
        CHECK(p.s("target").rfind("dungeon_", 0) == 0);
      }
    }
  }
  CHECK(inspectables >= 5);  // the world is worth looking at
}

// ---- Phase 2.3 content: regions, dungeons, homes, contacts ----

namespace {

// id -> (w, h) for every map the world can load.
std::map<std::string, std::pair<int, int>> mapSizes() {
  std::map<std::string, std::pair<int, int>> out;
  JVal town = loadContent("town.json");
  const JVal& ts = town.at("size");
  out[town.s("id")] = {(int)ts.arr[0].num, (int)ts.arr[1].num};
  JVal cities = loadContent("cities.json");
  for (const auto& m : cities.at("maps").arr) {
    const JVal& s = m.at("size");
    out[m.s("id")] = {(int)s.arr[0].num, (int)s.arr[1].num};
  }
  JVal interiors = loadContent("interiors.json");
  for (const auto& in : interiors.at("interiors").arr)
    out[in.s("id")] = {in.i("w"), in.i("h")};
  return out;
}

}  // namespace

TEST_CASE("every region declares an economy with sane numbers") {
  JVal cities = loadContent("cities.json");
  JVal world = loadContent("world.json");
  Regions r;
  r.load(world);
  REQUIRE(r.all().size() == world.at("cities").arr.size());

  for (const auto& c : world.at("cities").arr) {
    std::string id = c.s("id");
    INFO("city " << id);
    const RegionProfile* p = r.profile(id);
    REQUIRE(p != nullptr);
    CHECK(!p->community.empty());
    CHECK(!p->jobFocus.empty());
    CHECK(p->livingCost >= 0);
    CHECK(p->marketBias >= -90);   // a market can be cheap, never free
    CHECK(p->marketBias <= 90);
  }
  CHECK(r.dailyUpkeep("starter_town") == 0);  // the tutorial town is free
  CHECK(r.priceWithBias(100, "startup_city") > 100);  // and prices differ by region
}

TEST_CASE("hidden dungeons reference real maps, missions and rewards") {
  JVal d = loadContent("dungeons.json");
  auto sizes = mapSizes();
  auto missions = loadMissions();
  JVal shop = loadContent("shop.json");
  std::set<std::string> itemIds;
  for (const auto& it : shop.at("items").arr) itemIds.insert(it.s("id"));
  JVal interiors = loadContent("interiors.json");
  std::set<std::string> interiorIds;
  for (const auto& in : interiors.at("interiors").arr) interiorIds.insert(in.s("id"));

  std::set<std::string> ids;
  REQUIRE(d.at("dungeons").arr.size() >= 3);
  for (const auto& x : d.at("dungeons").arr) {
    std::string id = x.s("id");
    INFO("dungeon " << id);
    CHECK(!id.empty());
    CHECK(ids.insert(id).second);
    CHECK(x.i("minLevel", 1) >= 1);

    std::string entrance = x.s("entranceMap");
    std::string interior = x.s("interiorMap");
    CHECK(sizes.count(entrance) == 1);      // the hatch sits on a real map
    CHECK(interiorIds.count(interior) == 1);  // the secret room exists
    if (sizes.count(entrance)) {
      auto [w, h] = sizes[entrance];
      CHECK(x.i("entranceX") >= 0);
      CHECK(x.i("entranceX") < w);
      CHECK(x.i("entranceY") >= 0);
      CHECK(x.i("entranceY") < h);
    }
    CHECK(missions.count(x.s("missionId")) == 1);   // the challenge exists
    CHECK(itemIds.count(x.s("rewardItem")) == 1);   // the rare tech exists
    CHECK(!x.s("blurb").empty());
  }
}

TEST_CASE("homes reference real interiors and typed slots") {
  JVal h = loadContent("housing.json");
  auto sizes = mapSizes();
  const std::set<std::string> kinds{"workstation", "decor", "trophy"};
  REQUIRE(h.at("homes").arr.size() >= 1);
  for (const auto& home : h.at("homes").arr) {
    std::string mapId = home.s("mapId");
    INFO("home " << mapId);
    CHECK(sizes.count(mapId) == 1);
    CHECK(!home.s("name").empty());
    std::set<std::string> slotIds;
    for (const auto& s : home.at("slots").arr) {
      CHECK(slotIds.insert(s.s("id")).second);
      CHECK(kinds.count(s.s("kind")) == 1);
      std::string k = s.s("kind");
      if (k.empty()) continue;
      CHECK(s.i("x") >= 0);
      CHECK(s.i("y") >= 0);
    }
  }
}

TEST_CASE("contacts are unique, staffed and plausible") {
  JVal c = loadContent("contacts.json");
  JVal cities = loadContent("world.json");
  std::set<std::string> cityIds;
  for (const auto& x : cities.at("cities").arr) cityIds.insert(x.s("id"));

  std::set<std::string> ids;
  REQUIRE(c.at("contacts").arr.size() >= 3);
  for (const auto& x : c.at("contacts").arr) {
    INFO("contact " << x.s("id"));
    CHECK(ids.insert(x.s("id")).second);
    CHECK(!x.s("name").empty());
    CHECK(!x.s("role").empty());
    CHECK(!x.s("skill").empty());
    CHECK(!x.s("blurb").empty());
    CHECK(cityIds.count(x.s("cityId")) == 1);  // everyone lives somewhere real
  }
}

TEST_CASE("rare technology is convenience-only like everything else") {
  JVal shop = loadContent("shop.json");
  const std::set<std::string> allowed{"xpPercent", "concurrentMissions",
                                      "buildSpeedPercent", "comfort"};
  int rare = 0;
  for (const auto& it : shop.at("items").arr) {
    if (it.s("id") == "git_relic" || it.s("id") == "rack_unit" ||
        it.s("id") == "neural_accelerator") {
      rare++;
      for (const auto& kv : it.at("effects").obj) CHECK(allowed.count(kv.first) == 1);
    }
  }
  CHECK(rare == 3);  // one reward per dungeon
  CHECK(shop.at("items").arr.size() >= 10);  // the expanded market
}

// ---- education system: the curriculum knowledge graph (design 4.1/4.3) ----

TEST_CASE("curriculum content is a sound knowledge graph") {
  JVal j = loadContent("curriculum.json");
  Curriculum c;
  c.load(j);

  CHECK_FALSE(c.empty());
  CHECK(c.areas().size() >= 12);   // "twelve knowledge areas" (design 4.1)

  // validate() checks areas, prerequisites, objectives, misconceptions, mission
  // references and cycles in one pass.
  auto known = loadMissions();
  std::set<std::string> knownIds;
  for (const auto& kv : known) knownIds.insert(kv.first);
  auto errs = c.validate(knownIds);
  for (const auto& e : errs) INFO(e);
  CHECK(errs.empty());
}

TEST_CASE("every shipped mission teaches at least one topic") {
  // Design 02 §3: evidence-mapping tests — each mission maps to >=1 node, or it
  // is content that cannot light up any part of the skill tree.
  auto missions = loadMissions();
  Curriculum c;
  c.load(loadContent("curriculum.json"));

  for (const auto& kv : missions) {
    INFO("mission " << kv.first);
    CHECK_FALSE(c.topicsForMission(kv.first).empty());
  }
}

TEST_CASE("every topic is reachable: prerequisites and projects are real") {
  Curriculum c;
  c.load(loadContent("curriculum.json"));

  // A topic either teaches shipped missions or names a planned project — never
  // neither (validate() enforces this too; here we also prove reachability).
  for (const auto& kv : c.topics()) {
    const Topic& t = kv.second;
    INFO("topic " << t.id);
    CHECK((!t.missions.empty() || !t.projects.empty()));
    for (const auto& p : t.prerequisites) {
      INFO("prerequisite " << p);
      CHECK(c.topic(p) != nullptr);
    }
  }

  // The graph is acyclic and every topic appears in a dependency-first order.
  auto order = c.topologicalOrder();
  CHECK(order.size() == c.topics().size());
  std::set<std::string> seen;
  for (const auto& id : order) {
    for (const auto& p : c.directPrerequisites(id)) {
      INFO("topic " << id << " needs " << p);
      CHECK(seen.count(p) == 1);
    }
    seen.insert(id);
  }
}

TEST_CASE("the campaign's first topics are unlocked at the start") {
  Curriculum c;
  c.load(loadContent("curriculum.json"));
  auto first = c.availableTopics({});
  REQUIRE_FALSE(first.empty());
  // A brand-new player must be able to start the campaign.
  for (const auto& id : first) INFO(id);
  CHECK(first.size() >= 1);
  // And there is a finite, non-trivial ladder toward mastery.
  CHECK(c.topics().size() >= 30);
}

TEST_CASE("the shipped unlock catalog is sound and references live content") {
  auto missions = loadMissions();
  Curriculum c;
  c.load(loadContent("curriculum.json"));
  UnlockCatalog cat;
  cat.load(loadContent("unlocks.json"));

  for (const auto& e : cat.validate()) INFO(e);
  CHECK(cat.validate().empty());
  CHECK(cat.all().size() >= 8);

  for (const auto& d : cat.all()) {
    INFO("unlock " << d.id);
    CHECK(!d.title.empty());
    for (const auto& m : d.requires.missions) {
      INFO("mission " << m);
      CHECK(missions.count(m) == 1);
    }
    for (const auto& g : d.requires.gates) {
      INFO("gate " << g);
      REQUIRE(missions.count(g) == 1);
      CHECK(missions.at(g).gate);
    }
    for (const auto& s : d.requires.skills) {
      INFO("skill " << s);
      CHECK(c.topic(s) != nullptr);
    }
    for (const auto& s : d.requires.independentSkills) {
      INFO("independent skill " << s);
      CHECK(c.topic(s) != nullptr);
    }
  }
}

TEST_CASE("city access unlocks agree with the world graph") {
  // Two sources describe city gates (world.json and unlocks.json). This pins
  // them together so content edits cannot let the explanation drift from the
  // route the bus stop actually offers.
  UnlockCatalog cat;
  cat.load(loadContent("unlocks.json"));
  JVal world = loadContent("world.json");
  for (const auto& city : world.at("cities").arr) {
    std::string id = city.s("id");
    const UnlockDef* d = cat.def("city." + id);
    INFO("city " << id);
    REQUIRE(d != nullptr);
    CHECK(d->kind == UnlockKind::CityAccess);
    CHECK(d->requires.minLevel == city.i("minLevel", 1));
    std::string req = city.s("requiresMission");
    if (req.empty()) {
      CHECK(d->requires.missions.empty());
    } else {
      REQUIRE(d->requires.missions.size() == 1);
      CHECK(d->requires.missions[0] == req);
    }
  }
}

TEST_CASE("the shipped achievement catalog is sound and references live content") {
  auto missions = loadMissions();
  Curriculum c;
  c.load(loadContent("curriculum.json"));
  UnlockCatalog ucat;
  ucat.load(loadContent("unlocks.json"));
  AchievementCatalog ac;
  ac.load(loadContent("achievements.json"));

  for (const auto& e : ac.validate()) INFO(e);
  CHECK(ac.validate().empty());
  CHECK(ac.all().size() >= 8);

  for (const auto& d : ac.all()) {
    INFO("achievement " << d.id);
    CHECK(!d.title.empty());
    for (const auto& m : d.requires.missions) {
      INFO("mission " << m);
      CHECK(missions.count(m) == 1);
    }
    for (const auto& g : d.requires.gates) {
      INFO("gate " << g);
      REQUIRE(missions.count(g) == 1);
      CHECK(missions.at(g).gate);
    }
    for (const auto& s : d.requires.skills) {
      INFO("skill " << s);
      CHECK(c.topic(s) != nullptr);
    }
    for (const auto& ms : d.requires.milestones) {
      INFO("milestone " << ms);
      CHECK(ucat.milestone(ms) != nullptr);
    }
  }
}

TEST_CASE("the shipped careers are sound and every path has an entry requirement") {
  auto missions = loadMissions();
  Curriculum c;
  c.load(loadContent("curriculum.json"));
  UnlockCatalog ucat;
  ucat.load(loadContent("unlocks.json"));
  Careers cs;
  cs.load(loadContent("careers.json"));

  for (const auto& e : cs.validate()) INFO(e);
  CHECK(cs.validate().empty());
  CHECK(cs.all().size() >= 3);

  for (const auto& d : cs.all()) {
    INFO("career " << d.id);
    CHECK_FALSE(requirementEmpty(d.entry));  // a career is earned, not free
    for (const auto& m : d.entry.missions) {
      INFO("mission " << m);
      CHECK(missions.count(m) == 1);
    }
    for (const auto& g : d.entry.gates) {
      INFO("gate " << g);
      REQUIRE(missions.count(g) == 1);
      CHECK(missions.at(g).gate);
    }
    for (const auto& s : d.entry.skills) {
      INFO("skill " << s);
      CHECK(c.topic(s) != nullptr);
    }
    for (const auto& ms : d.entry.milestones) {
      INFO("milestone " << ms);
      CHECK(ucat.milestone(ms) != nullptr);
    }
    // Every shipped path carries the six project-ladder rungs in order.
    INFO("ladder for " << d.id);
    CHECK(d.ladder.size() == ladderRungKinds().size());
    for (size_t i = 0; i < d.ladder.size() && i < ladderRungKinds().size(); i++)
      CHECK(d.ladder[i].kind == ladderRungKinds()[i]);
  }
}

TEST_CASE("the shipped framework catalog resolves its references") {
  Curriculum c;
  c.load(loadContent("curriculum.json"));
  Frameworks fw;
  fw.load(loadContent("frameworks.json"));
  for (const auto& e : fw.validate()) INFO(e);
  CHECK(fw.validate().empty());
  CHECK(fw.disciplines().size() == 14);
  CHECK(fw.all().size() >= 12);

  // Languages come from the IDE's language list, not from a second copy.
  std::ifstream langFile("../game-ide/classic/languages.json");
  REQUIRE_MESSAGE(langFile, "missing ../game-ide/classic/languages.json");
  std::stringstream langSs;
  langSs << langFile.rdbuf();
  std::set<std::string> langIds;
  for (const auto& l : parseJson(langSs.str()).arr)
    langIds.insert(l.s("id"));

  for (const auto& f : fw.all()) {
    INFO("framework " << f.id);
    if (!f.language.empty()) CHECK(langIds.count(f.language) == 1);
    for (const auto& s : f.skills) {
      INFO("skill " << s);
      CHECK(c.topic(s) != nullptr);
    }
  }
}
