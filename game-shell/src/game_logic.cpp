// game.cpp (part 3) — missions, XP/levels, ledger, dialogue, UI events.
// The Proof-of-Skill loop: accept -> code in the real IDE -> submit ->
// IDE's test path grades (public + hidden) -> rewards + quest state.

#include "game.h"
#include "bridge.h"
#include "util.h"

namespace dw {

std::string Game::missionsJson() {
  std::string s = "{\"missions\":[";
  bool first = true;
  for (auto& kv : missions_) {
    const MissionDef& m = kv.second;
    MissionState st = MissionState::Locked;
    auto it = player_.missions.find(m.id);
    if (it != player_.missions.end()) st = it->second.state;
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(m.id) + ",\"title\":" + json_escape(m.title) +
         ",\"giver\":" + json_escape(m.giver) + ",\"briefing\":" + json_escape(m.briefing) +
         ",\"language\":" + json_escape(m.language) +
         ",\"xp\":" + std::to_string(m.xpReward) +
         ",\"credits\":" + std::to_string(m.creditReward) +
         ",\"tests\":" + std::to_string(m.tests.size()) +
         ",\"state\":" + std::to_string((int)st) + "}";
  }
  return s + "]}";
}

std::string Game::questJson() {
  std::string s = "{\"active\":[";
  bool first = true;
  for (auto& kv : player_.missions) {
    if (kv.second.state != MissionState::Active && kv.second.state != MissionState::Submitted)
      continue;
    const MissionDef& m = missions_[kv.first];
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(m.id) + ",\"title\":" + json_escape(m.title) +
         ",\"briefing\":" + json_escape(m.briefing) + "}";
  }
  s += "],\"complete\":[";
  first = true;
  for (auto& kv : player_.missions) {
    if (kv.second.state != MissionState::Complete) continue;
    if (!first) s += ",";
    first = false;
    s += json_escape(missions_[kv.first].title);
  }
  return s + "]}";
}

std::string Game::npcDialogueJson(const std::string& npcId, const std::string& nodeId) {
  for (auto& n : npcs_) {
    if (n.id != npcId) continue;
    std::string nid = nodeId.empty() ? n.rootNode : nodeId;
    auto it = n.nodes.find(nid);
    if (it == n.nodes.end()) return "{}";
    const DialogueNode& node = it->second;
    std::string s = "{\"npc\":" + json_escape(n.name) + ",\"npcId\":" + json_escape(n.id) +
                    ",\"text\":" + json_escape(node.text) + ",\"options\":[";
    bool first = true;
    for (auto& o : node.options) {
      // "@mission:X" options only appear if the mission is available/active.
      if (o.next.rfind("@mission:", 0) == 0) {
        std::string mid = o.next.substr(9);
        auto mi = player_.missions.find(mid);
        if (mi == player_.missions.end() || mi->second.state == MissionState::Complete ||
            mi->second.state == MissionState::Locked)
          continue;
      }
      if (!first) s += ",";
      first = false;
      s += "{\"text\":" + json_escape(o.text) + ",\"next\":" + json_escape(o.next) + "}";
    }
    return s + "]}";
  }
  return "{}";
}

void Game::acceptMission(const std::string& id) {
  auto it = missions_.find(id);
  if (it == missions_.end()) return;
  MissionProgress& pr = player_.missions[id];
  if (pr.state != MissionState::Available) return;
  pr.state = MissionState::Active;
  // Seed the IDE now so the file + tests are waiting at any computer.
  ide_seed_mission(it->second);
  notify("Mission accepted: " + it->second.title + ". Find a computer to code.", "quest");
  sfx("success");
  ui_update_hud();
  saveNow();
}

void Game::addXp(int64_t xp) {
  player_.levels.xp += xp;
  bool leveled = false;
  while (player_.levels.xp >= LevelState::xpForLevel(player_.levels.level)) {
    player_.levels.xp -= LevelState::xpForLevel(player_.levels.level);
    player_.levels.level++;
    leveled = true;
  }
  if (leveled) {
    notify("LEVEL UP — you are now level " + std::to_string(player_.levels.level) + "!", "levelup");
    sfx("levelup");
    player_.achievements.push_back("level_" + std::to_string(player_.levels.level));
  }
  ui_update_hud();
}

void Game::ledgerAdd(const std::string& kind, const std::string& memo, int64_t delta) {
  // [PHASE2] ledger rows are created by the economy service; the client sends
  // signed intents. Here the ledger IS local (single-player Phase 1).
  LedgerRow r;
  r.id = make_id(); r.ts = iso_now();
  r.kind = kind; r.memo = memo; r.delta = delta;
  player_.ledger.push_back(r);
}

void Game::completeMission(const std::string& id, int passed, int total) {
  auto it = missions_.find(id);
  if (it == missions_.end()) return;
  MissionDef& m = it->second;
  MissionProgress& pr = player_.missions[id];
  pr.attempts++;
  if (passed < total) {
    pr.state = MissionState::Active;  // keep trying
    notify("Tests: " + std::to_string(passed) + "/" + std::to_string(total) +
           " passed. The town believes in you — check the IDE output and try again.", "error");
    sfx("error");
    return;
  }
  pr.state = MissionState::Complete;
  addXp(m.xpReward);
  ledgerAdd("mission_reward", "Mission complete: " + m.title, m.creditReward);
  player_.reputation += 5;
  notify("Mission complete: " + m.title + " (+" + std::to_string(m.xpReward) + " XP, +" +
         std::to_string(m.creditReward) + " credits)", "success");
  sfx("coin");
  // Unlock follow-ups.
  for (auto& kv : missions_) {
    MissionProgress& p2 = player_.missions[kv.first];
    if (p2.state != MissionState::Locked) continue;
    bool ok = true;
    for (auto& req : kv.second.requires) {
      auto ri = player_.missions.find(req);
      if (ri == player_.missions.end() || ri->second.state != MissionState::Complete) { ok = false; break; }
    }
    if (ok) {
      p2.state = MissionState::Available;
      notify("New mission available: " + kv.second.title, "quest");
    }
  }
  ui_update_hud();
  saveNow();
}

void Game::buyItem(const std::string& id) {  for (auto& it : shopItems_) {
    if (it.id != id) continue;
    if (player_.balance() < it.price) {
      notify("Not enough credits for " + it.name + ".", "error");
      sfx("error");
      return;
    }
    // [PHASE2] purchases are server-validated (price, ownership, funds) with
    // idempotency keys. Phase 1: local double-entry.
    ledgerAdd("shop_buy", "Bought " + it.name, -it.price);
    player_.inventory[it.id]++;
    notify("Bought " + it.name + ".", "success");
    sfx("coin");
    ui_update_hud();
    saveNow();
    return;
  }
}

// ---- JS -> C++ UI events (JSON: {"type": ..., ...}) ----
void Game::uiEvent(const std::string& json) {
  JVal j;
  try { j = parseJson(json); } catch (...) { return; }
  std::string t = j.s("type");
  if (t == "char_done") {
    player_.name = j.s("name", "Dev");
    player_.displayName = j.s("display", player_.name);
    auto col = [&](const std::string& k, Color d) {
      const JVal& a = j.at(k);
      if (a.type == JVal::ARR && a.arr.size() >= 3)
        return Color{(uint8_t)a.arr[0].num, (uint8_t)a.arr[1].num, (uint8_t)a.arr[2].num};
      return d;
    };
    player_.skin = col("skin", player_.skin);
    player_.shirt = col("shirt", player_.shirt);
    player_.pants = col("pants", player_.pants);
    ui_hide("charcreate");
    uiOpen_ = false;
    // Starter ledger: the town grants every new dev 20 credits, no strings.
    ledgerAdd("grant", "Starter grant from the town", 20);
    notify("Welcome to Starter Town, " + player_.displayName + "! Press E at the glowing spots. The mission board is at the plaza.", "quest");
    sfx("success");
    ui_update_hud();
    saveNow();
  } else if (t == "panel_closed") {
    uiOpen_ = false;
    if (ide_is_open()) { /* IDE overlay manages itself */ }
  } else if (t == "mission_accept") {
    acceptMission(j.s("id"));
    ui_show("missionboard", missionsJson());  // refresh
  } else if (t == "mission_submit") {
    auto it = missions_.find(j.s("id"));
    if (it != missions_.end() && player_.missions[it->first].state == MissionState::Active) {
      pendingMission_ = it->first;
      player_.missions[it->first].state = MissionState::Submitted;
      ide_submit_mission(it->second);
      notify("Running tests in the IDE…", "info");
    }
  } else if (t == "dialogue_choice") {
    std::string next = j.s("next");
    std::string npc = j.s("npc");
    if (next == "@close" || next == "@bye") { ui_hide("dialogue"); uiOpen_ = false; }
    else if (next.rfind("@mission:", 0) == 0) {
      ui_hide("dialogue"); uiOpen_ = false;
      acceptMission(next.substr(9));
    } else if (next == "@shop") { ui_hide("dialogue"); ui_show("shop", "{}"); }
    else {
      // sets= flag handling would go here (dialogue options carry it)
      ui_show("dialogue", npcDialogueJson(npc, next));
    }
    sfx("interact");
  } else if (t == "shop_buy") {
    buyItem(j.s("id"));
    ui_show("shop", panelJson("shop"));  // refresh balance
  } else if (t == "ide_closed") {
    ui_hide("idebar");
  } else if (t == "keybind") {
    auto it = keys_.find(j.s("action"));
    if (it != keys_.end()) it->second = (SDL_Keycode)j.i("key");
    saveNow();
  }
}

void Game::missionTestResult(int passed, int total) {
  if (pendingMission_.empty()) return;
  std::string id = pendingMission_;
  pendingMission_.clear();
  completeMission(id, passed, total);
}

void Game::setKeybind(const std::string& action, int keycode) {
  auto it = keys_.find(action);
  if (it != keys_.end()) it->second = (SDL_Keycode)keycode;
}

void Game::touchMove(const std::string& dir, bool down) {
  if (dir == "up") moveUp_ = down;
  else if (dir == "down") moveDown_ = down;
  else if (dir == "left") moveLeft_ = down;
  else if (dir == "right") moveRight_ = down;
}

void Game::queueSeed(const std::string& missionId) {
  for (auto& s : pendingSeeds_)
    if (s == missionId) return;
  pendingSeeds_.push_back(missionId);
}

void Game::ideReady() {
  // The IDE iframe finished loading: flush any missions that couldn't be
  // seeded earlier (accepted before the player ever touched a computer).
  for (auto& id : pendingSeeds_) {
    auto it = missions_.find(id);
    if (it != missions_.end()) ide_seed_mission(it->second);
  }
  pendingSeeds_.clear();
}

// ---- panel data for shell.js (JSON payloads with ui_show) ----
std::string Game::panelJson(const std::string& panel) {
  if (panel == "shop") {
    std::string s = "{\"balance\":" + std::to_string(player_.balance()) + ",\"items\":[";
    bool first = true;
    for (auto& it : shopItems_) {
      if (!first) s += ",";
      first = false;
      int owned = 0;
      auto oi = player_.inventory.find(it.id);
      if (oi != player_.inventory.end()) owned = oi->second;
      s += "{\"id\":" + json_escape(it.id) + ",\"name\":" + json_escape(it.name) +
           ",\"desc\":" + json_escape(it.desc) + ",\"price\":" + std::to_string(it.price) +
           ",\"owned\":" + std::to_string(owned) + "}";
    }
    return s + "]}";
  }
  if (panel == "bank") {
    std::string s = "{\"balance\":" + std::to_string(player_.balance()) + ",\"rows\":[";
    bool first = true;
    for (auto it = player_.ledger.rbegin(); it != player_.ledger.rend(); ++it) {
      if (!first) s += ",";
      first = false;
      s += "{\"ts\":" + json_escape(it->ts) + ",\"kind\":" + json_escape(it->kind) +
           ",\"memo\":" + json_escape(it->memo) + ",\"delta\":" + std::to_string(it->delta) + "}";
    }
    return s + "]}";
  }
  if (panel == "inventory") {
    std::string s = "{\"items\":[";
    bool first = true;
    for (auto& kv : player_.inventory) {
      std::string nm = kv.first, ds;
      for (auto& it : shopItems_)
        if (it.id == kv.first) { nm = it.name; ds = it.desc; break; }
      if (!first) s += ",";
      first = false;
      s += "{\"id\":" + json_escape(kv.first) + ",\"name\":" + json_escape(nm) +
           ",\"desc\":" + json_escape(ds) + ",\"qty\":" + std::to_string(kv.second) + "}";
    }
    return s + "]}";
  }
  if (panel == "worldmap") {
    // Points of interest for the overlay map.
    return "{\"pois\":["
           "{\"name\":\"Your Apartment\",\"x\":7,\"y\":10},"
           "{\"name\":\"Computer Lab\",\"x\":21,\"y\":8},"
           "{\"name\":\"Mission Hall\",\"x\":32,\"y\":10},"
           "{\"name\":\"Fix-It Shop\",\"x\":44,\"y\":8},"
           "{\"name\":\"Data Kiosk\",\"x\":52,\"y\":22},"
           "{\"name\":\"Market\",\"x\":27,\"y\":33},"
           "{\"name\":\"Bus Stop (locked)\",\"x\":53,\"y\":37},"
           "{\"name\":\"Mission Board\",\"x\":32,\"y\":18}]}";
  }
  if (panel == "skilltree") {
    int64_t need = LevelState::xpForLevel(player_.levels.level);
    return "{\"level\":" + std::to_string(player_.levels.level) +
           ",\"xp\":" + std::to_string(player_.levels.xp) +
           ",\"need\":" + std::to_string(need) +
           ",\"reputation\":" + std::to_string(player_.reputation) + "}";
  }
  if (panel == "hudstate") {
    int64_t need = LevelState::xpForLevel(player_.levels.level);
    int active = 0;
    for (auto& kv : player_.missions)
      if (kv.second.state == MissionState::Active) active++;
    int hh = (int)(dayT_ * 24), mm = (int)((dayT_ * 24 - hh) * 60);
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", hh, mm);
    return "{\"name\":" + json_escape(player_.displayName) +
           ",\"level\":" + std::to_string(player_.levels.level) +
           ",\"xp\":" + std::to_string(player_.levels.xp) +
           ",\"need\":" + std::to_string(need) +
           ",\"credits\":" + std::to_string(player_.balance()) +
           ",\"quests\":" + std::to_string(active) + ",\"clock\":" + json_escape(clock) +
           ",\"map\":" + json_escape(world_.map(curMap_).name) + "}";
  }
  return "{}";
}

}  // namespace dw
