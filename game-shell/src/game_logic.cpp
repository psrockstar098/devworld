// game.cpp (part 3) — missions, XP/levels, ledger, dialogue, UI events.
// The Proof-of-Skill loop: accept -> code in the real IDE -> submit ->
// IDE's test path grades (public + hidden) -> rewards + quest state.

#include "game.h"
#include "bridge.h"
#include "progression.h"
#include "equipment.h"
#include "profile.h"
#include "util.h"
#include <algorithm>

namespace dw {

// Every player-facing message goes through here so it lands both on screen and
// in the capped notification log (see the notifications panel).
void Game::notice(const std::string& text, const std::string& kind) {
  Notice n;
  n.type = kind;
  n.text = text;
  n.at = iso_now();
  pushNotice(player_.notices, n);
  notify(text, kind);
}

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
         ",\"level\":" + std::to_string(m.level) +
         ",\"gate\":" + (m.gate ? "true" : "false") +
         ",\"hints\":" + (m.hints ? "true" : "false") +
         ",\"xp\":" + std::to_string(m.xpReward) +
         ",\"credits\":" + std::to_string(m.creditReward) +
         ",\"tests\":" + std::to_string(m.tests.size()) +
         ",\"state\":" + std::to_string((int)st) + "}";
  }
  return s + "]}";
}

std::string Game::questJson() {
  std::string s = "{\"tracked\":" + json_escape(player_.trackedMission) + ",\"active\":[";
  bool first = true;
  for (auto& kv : player_.missions) {
    if (kv.second.state != MissionState::Active && kv.second.state != MissionState::Submitted)
      continue;
    const MissionDef& m = missions_[kv.first];
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(m.id) + ",\"title\":" + json_escape(m.title) +
         ",\"briefing\":" + json_escape(m.briefing) +
         ",\"objective\":" + json_escape(objectiveFor(m, kv.second)) +
         ",\"tracked\":" + (player_.trackedMission == m.id ? "true" : "false") +
         ",\"level\":" + std::to_string(m.level) +
         ",\"gate\":" + (m.gate ? "true" : "false") + "}";
  }
  s += "],\"complete\":[";
  first = true;
  for (auto& kv : player_.missions) {
    if (kv.second.state != MissionState::Complete) continue;
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(missions_[kv.first].id) +
         ",\"title\":" + json_escape(missions_[kv.first].title) + "}";
  }
  return s + "]}";
}

std::string Game::travelJson() {
  const CityDef* here = currentCity();
  std::string s = "{\"city\":" + json_escape(here ? here->name : "Unknown") +
                  ",\"region\":" + json_escape(here ? here->region : "") +
                  ",\"balance\":" + std::to_string(player_.balance()) + ",\"offers\":[";
  bool first = true;
  for (const auto& o : graph_.offers(currentCityId(), player_.levels.level, player_.missions)) {
    const CityDef* c = graph_.city(o.toCity);
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(o.toCity) + ",\"name\":" + json_escape(o.toName) +
         ",\"map\":" + json_escape(o.toMap) + ",\"fare\":" + std::to_string(o.fare) +
         ",\"unlocked\":" + (o.unlocked ? "true" : "false") +
         ",\"reason\":" + json_escape(o.reason) +
         ",\"blurb\":" + json_escape(c ? c->blurb : "") +
         ",\"region\":" + json_escape(c ? c->region : "") + "}";
  }
  return s + "]}";
}

const CityDef* Game::currentCity() const { return graph_.cityForMap(curMap_); }

std::string Game::currentCityId() const {
  std::string id = graph_.cityIdForMap(curMap_);
  return id.empty() ? "starter_town" : id;
}

void Game::travelTo(const std::string& cityId) {
  std::vector<TravelOffer> offers =
      graph_.offers(currentCityId(), player_.levels.level, player_.missions);
  const TravelOffer* pick = nullptr;
  for (const auto& o : offers)
    if (o.toCity == cityId) { pick = &o; break; }
  if (!pick) { notice("No bus route to that city.", "error"); return; }
  if (!pick->unlocked) { notice("Route locked — " + pick->reason + ".", "error"); sfx("error"); return; }
  if (player_.balance() < pick->fare) {
    notice("Not enough credits for the fare (" + std::to_string(pick->fare) + " cr).", "error");
    sfx("error");
    return;
  }
  // [PHASE2] travel is an idempotency-keyed call to the world-sim service;
  // the server owns unlocks and the ledger. Phase 1: local double-entry.
  ledgerAdd("bus_fare", "Bus to " + pick->toName, -pick->fare);
  ui_hide("all");
  uiOpen_ = false;
  enterMap(pick->toMap, -1, -1);  // map spawn (bus stop)
  if (std::find(player_.achievements.begin(), player_.achievements.end(), "visited_" + cityId) ==
      player_.achievements.end())
    player_.achievements.push_back("visited_" + cityId);
  notice("Arrived in " + pick->toName + ". Fare: " + std::to_string(pick->fare) + " cr.", "success");
  sfx("success");
  saveNow();
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
      s += "{\"text\":" + json_escape(o.text) + ",\"next\":" + json_escape(o.next) +
           ",\"sets\":" + json_escape(o.sets) + "}";
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
  int limit = Equipment::missionSlotLimit(player_, catalog_);
  if (countActiveMissions(player_.missions) >= limit) {
    notice("You can hold " + std::to_string(limit) + " active mission" +
               (limit == 1 ? "" : "s") +
               " right now. Finish one first (better gear adds slots).",
           "error");
    sfx("error");
    return;
  }
  pr.state = MissionState::Active;
  if (player_.trackedMission.empty()) player_.trackedMission = id;
  // Seed the IDE now so the file + tests are waiting at any computer.
  ide_seed_mission(it->second);
  notice("Mission accepted: " + it->second.title + ". Find a computer to code.", "quest");
  sfx("success");
  ui_update_hud();
  saveNow();
}

void Game::addXp(int64_t xp) {
  XpResult r = applyXp(player_.levels, xp);
  if (r.levelsGained > 0) {
    for (int lvl = r.level - r.levelsGained + 1; lvl <= r.level; ++lvl)
      player_.achievements.push_back("level_" + std::to_string(lvl));
    notice("LEVEL UP — you are now level " + std::to_string(r.level) + " (" +
               titleForLevel(r.level) + ")!",
           "levelup");
    sfx("levelup");
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
    notice("Tests: " + std::to_string(passed) + "/" + std::to_string(total) +
           " passed. The town believes in you — check the IDE output and try again.", "error");
    sfx("error");
    return;
  }
  pr.state = MissionState::Complete;
  EquipEffects fx = Equipment::effects(player_, catalog_);
  int64_t reward = m.xpReward + m.xpReward * fx.xpPercent / 100;
  addXp(reward);
  ledgerAdd("mission_reward", "Mission complete: " + m.title, m.creditReward);
  player_.reputation += 5;
  notice("Mission complete: " + m.title + " (+" + std::to_string(reward) + " XP" +
             (fx.xpPercent > 0 ? ", +" + std::to_string(fx.xpPercent) + "% gear bonus" : "") +
             ", +" + std::to_string(m.creditReward) + " credits)",
         "success");
  sfx("coin");
  if (player_.trackedMission == id) player_.trackedMission.clear();
  // Hidden technology: a dungeon's challenge pays out its rare technology.
  for (const auto& d : dungeons_.all())
    if (d.missionId == id) grantDungeonReward(d);
  // Unlock follow-ups.
  std::vector<std::string> unlocked = eligibleUnlocks(missions_, player_.missions);
  for (const auto& mid : unlocked) {
    player_.missions[mid].state = MissionState::Available;
    notice("New mission available: " + missions_[mid].title, "quest");
  }
  ui_update_hud();
  saveNow();
}

void Game::buyItem(const std::string& id) {
  for (auto& it : shopItems_) {
    if (it.id != id) continue;
    // Regional technology markets: the same part costs more or less by city.
    int64_t price = regions_.priceWithBias(it.price, currentCityId());
    if (player_.balance() < price) {
      notice("Not enough credits for " + it.name + ".", "error");
      sfx("error");
      return;
    }
    // [PHASE2] purchases are server-validated (price, ownership, funds) with
    // idempotency keys. Phase 1: local double-entry.
    ledgerAdd("shop_buy", "Bought " + it.name, -price);
    player_.inventory[it.id]++;
    notice("Bought " + it.name + " for " + std::to_string(price) + " cr.", "success");
    sfx("coin");
    ui_update_hud();
    saveNow();
    return;
  }
}

// ---- identity, equipment, accessibility (2.2) ----

void Game::equipItem(const std::string& itemId) {
  EquipResult r = Equipment::equip(player_, itemId, catalog_);
  if (!r.ok) {
    notice(r.reason, "error");
    sfx("error");
    return;
  }
  notice("Equipped " + catalog_[itemId].name + ".", "success");
  sfx("success");
  ui_update_hud();
  saveNow();
}

void Game::unequipSlot(const std::string& slot) {
  EquipResult r = Equipment::unequip(player_, slot);
  if (!r.ok) {
    notice(r.reason, "error");
    sfx("error");
    return;
  }
  notice("Unequipped " + slot + ".", "info");
  sfx("close");
  ui_update_hud();
  saveNow();
}

void Game::trackMission(const std::string& id) {
  if (player_.trackedMission == id) {
    player_.trackedMission.clear();
    notice("Objective tracker cleared.", "info");
  } else if (missions_.count(id)) {
    player_.trackedMission = id;
    notice("Tracking: " + missions_[id].title, "quest");
  }
  ui_update_hud();
  saveNow();
}

void Game::applySettings(const JVal& j) {
  player_.a11y = AccessibilitySettings::parse(j);
  saveNow();
}

// ---- panel payloads for the 2.2 screens ----

std::string Game::profileJson() {
  int complete = 0, active = 0;
  for (const auto& kv : player_.missions) {
    if (kv.second.state == MissionState::Complete) complete++;
    else if (kv.second.state == MissionState::Active || kv.second.state == MissionState::Submitted) active++;
  }
  ProfileSummary p = profileFor(player_, complete, active);
  EquipEffects fx = Equipment::effects(player_, catalog_);

  std::string s = "{\"name\":" + json_escape(p.name) +
                  ",\"displayName\":" + json_escape(p.displayName) +
                  ",\"title\":" + json_escape(p.title) +
                  ",\"level\":" + std::to_string(p.level) +
                  ",\"xp\":" + std::to_string(p.xp) +
                  ",\"need\":" + std::to_string(p.xpToNext) +
                  ",\"reputation\":" + std::to_string(p.reputation) +
                  ",\"reputationTier\":" + json_escape(p.reputationTier) +
                  ",\"missionsComplete\":" + std::to_string(p.missionsComplete) +
                  ",\"missionsActive\":" + std::to_string(p.missionsActive) +
                  ",\"playMinutes\":" + std::to_string(p.playMinutes) +
                  ",\"missionSlots\":" + std::to_string(Equipment::missionSlotLimit(player_, catalog_)) +
                  ",\"xpBonus\":" + std::to_string(fx.xpPercent) +
                  ",\"comfort\":" + std::to_string(fx.comfort) +
                  ",\"buildSpeed\":" + std::to_string(fx.buildSpeedPercent) +
                  ",\"achievements\":[";
  bool first = true;
  for (const auto& a : p.achievements) {
    if (!first) s += ",";
    first = false;
    s += json_escape(a);
  }
  s += "],\"visited\":[";
  first = true;
  for (const auto& c : p.visitedCities) {
    if (!first) s += ",";
    first = false;
    s += json_escape(c);
  }
  s += "],\"loadout\":[";
  first = true;
  for (const auto& l : p.loadout) {
    if (!first) s += ",";
    first = false;
    s += json_escape(l);
  }
  return s + "]}";
}

std::string Game::settingsJson() {
  std::string s = "{\"a11y\":" + player_.a11y.toJson() + ",\"keybinds\":{";
  bool first = true;
  for (const auto& kv : keys_) {
    if (!first) s += ",";
    first = false;
    s += json_escape(kv.first) + ":" + std::to_string((int)kv.second);
  }
  return s + "}}";
}

std::string Game::notificationsJson() {
  std::string s = "{\"items\":[";
  bool first = true;
  for (auto it = player_.notices.rbegin(); it != player_.notices.rend(); ++it) {
    if (!first) s += ",";
    first = false;
    s += "{\"type\":" + json_escape(it->type) + ",\"text\":" + json_escape(it->text) +
         ",\"at\":" + json_escape(it->at) + "}";
  }
  return s + "]}";
}

std::string Game::examineJson(const std::string& id) {
  const TileMap& m = world_.map(curMap_);
  for (const auto& it : m.interactables) {
    if (it.id != id) continue;
    return "{\"title\":" + json_escape(it.label) + ",\"text\":" + json_escape(it.text) + "}";
  }
  return "{\"title\":\"\",\"text\":\"\"}";
}


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
    player_.hair = col("hair", player_.hair);
    ui_hide("charcreate");
    uiOpen_ = false;
    // Starter ledger: the town grants every new dev 20 credits, no strings.
    ledgerAdd("grant", "Starter grant from the town", 20);
    notice("Welcome to Starter Town, " + player_.displayName + "! Press E at the glowing spots. The mission board is at the plaza.", "quest");
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
      notice("Running tests in the IDE…", "info");
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
      // A chosen option can set a world flag (rumours reveal dungeons).
      std::string sets = j.s("sets");
      if (!sets.empty()) revealDungeon(sets);
      ui_show("dialogue", npcDialogueJson(npc, next));
    }
    sfx("interact");
  } else if (t == "shop_buy") {
    buyItem(j.s("id"));
    ui_show("shop", panelJson("shop"));  // refresh balance
  } else if (t == "travel") {
    travelTo(j.s("id"));
  } else if (t == "equip") {
    equipItem(j.s("id"));
    ui_show("inventory", panelJson("inventory"));  // refresh slots
  } else if (t == "unequip") {
    unequipSlot(j.s("slot"));
    ui_show("inventory", panelJson("inventory"));
  } else if (t == "track") {
    trackMission(j.s("id"));
    ui_show("quests", questJson());
  } else if (t == "a11y") {
    applySettings(j.at("a11y"));
    ui_show("settings", settingsJson());
  } else if (t == "notifications_clear") {
    player_.notices.clear();
    saveNow();
    ui_show("notifications", notificationsJson());
  } else if (t == "examine") {
    // Object lore is opened directly by the world; nothing to persist.
  } else if (t == "home_place") {
    placeHome(j.s("slot"), j.s("item"));
    ui_show("home", homeJson());
  } else if (t == "home_clear") {
    clearHome(j.s("slot"));
    ui_show("home", homeJson());
  } else if (t == "trophy") {
    toggleTrophy(j.s("id"));
    ui_show("home", homeJson());
  } else if (t == "contact_open") {
    std::string cid = j.s("id");
    Social::markRead(player_, cid);
    ui_show("contacts", contactsJson(cid));
  } else if (t == "message") {
    std::string cid = j.s("contact");
    sendMessage(cid, j.s("text"));
    Social::markRead(player_, cid);
    ui_show("contacts", contactsJson(cid));
  } else if (t == "team_add") {
    std::string cid = j.s("contact");
    teamChange(j.s("id"), true);
    ui_show("contacts", contactsJson(cid));
  } else if (t == "team_remove") {
    std::string cid = j.s("contact");
    teamChange(j.s("id"), false);
    ui_show("contacts", contactsJson(cid));
  } else if (t == "hire") {
    hireContact(j.s("id"), j.boolean("add"));
    ui_show("company", companyJson());
  } else if (t == "company_found") {
    foundCompany(j.s("name"), j.s("focus"));
    ui_show("company", companyJson());
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
    const RegionProfile* reg = regions_.profile(currentCityId());
    std::string s = "{\"balance\":" + std::to_string(player_.balance()) +
                    ",\"community\":" + json_escape(reg ? reg->community : std::string("")) +
                    ",\"bias\":" + std::to_string(reg ? reg->marketBias : 0) + ",\"items\":[";
    bool first = true;
    for (auto& it : shopItems_) {
      if (!first) s += ",";
      first = false;
      int owned = 0;
      auto oi = player_.inventory.find(it.id);
      if (oi != player_.inventory.end()) owned = oi->second;
      int64_t price = regions_.priceWithBias(it.price, currentCityId());
      s += "{\"id\":" + json_escape(it.id) + ",\"name\":" + json_escape(it.name) +
           ",\"desc\":" + json_escape(it.desc) + ",\"price\":" + std::to_string(price) +
           ",\"base\":" + std::to_string(it.price) +
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
    auto effectText = [](const ItemEffects& e) {
      std::string t;
      auto add = [&](int v, const std::string& s) {
        if (v == 0) return;
        if (!t.empty()) t += ", ";
        t += "+" + std::to_string(v) + s;
      };
      add(e.xpPercent, "% mission XP");
      add(e.concurrentMissions, " mission slot" + std::string(e.concurrentMissions == 1 ? "" : "s"));
      add(e.buildSpeedPercent, "% build speed");
      add(e.comfort, " comfort");
      return t.empty() ? std::string("no bonus") : t;
    };

    const std::vector<std::string>& slots = Equipment::slots();
    std::string s = "{\"balance\":" + std::to_string(player_.balance()) + ",\"slots\":[";
    bool first = true;
    for (const auto& slot : slots) {
      if (!first) s += ",";
      first = false;
      std::string itemId = Equipment::equippedIn(player_, slot);
      auto ci = catalog_.find(itemId);
      s += "{\"slot\":" + json_escape(slot) +
           ",\"item\":" + json_escape(itemId) +
           ",\"name\":" + json_escape(ci != catalog_.end() ? ci->second.name : std::string("")) +
           ",\"empty\":" + (itemId.empty() ? "true" : "false") + "}";
    }
    s += "],\"items\":[";
    first = true;
    for (auto& kv : player_.inventory) {
      if (kv.second <= 0) continue;
      std::string nm = kv.first, ds, slot;
      ItemEffects fx;
      for (auto& it : shopItems_)
        if (it.id == kv.first) { nm = it.name; ds = it.desc; slot = it.slot; fx = it.effects; break; }
      bool equippable = Equipment::isSlot(slot);
      bool equipped = false;
      for (auto& eq : player_.equipped)
        if (eq.second == kv.first) equipped = true;
      if (!first) s += ",";
      first = false;
      s += "{\"id\":" + json_escape(kv.first) + ",\"name\":" + json_escape(nm) +
           ",\"desc\":" + json_escape(ds) + ",\"qty\":" + std::to_string(kv.second) +
           ",\"slot\":" + json_escape(slot) +
           ",\"equippable\":" + (equippable ? "true" : "false") +
           ",\"equipped\":" + (equipped ? "true" : "false") +
           ",\"effect\":" + json_escape(effectText(fx)) + "}";
    }
    EquipEffects tot = Equipment::effects(player_, catalog_);
    s += "],\"totals\":{\"xpBonus\":" + std::to_string(tot.xpPercent) +
         ",\"missionSlots\":" + std::to_string(Equipment::missionSlotLimit(player_, catalog_)) +
         ",\"comfort\":" + std::to_string(tot.comfort) +
         ",\"buildSpeed\":" + std::to_string(tot.buildSpeedPercent) + "}}";
    return s;
  }
  if (panel == "profile") return profileJson();
  if (panel == "settings") return settingsJson();
  if (panel == "notifications") return notificationsJson();
  if (panel == "portfolio") return portfolioJson();
  if (panel == "company") return companyJson();
  if (panel == "home") return homeJson();
  if (panel.rfind("contacts", 0) == 0) {
    std::string cid = panel.size() > 9 ? panel.substr(9) : "";  // "contacts:<id>"
    if (!cid.empty()) Social::markRead(player_, cid);
    return contactsJson(cid);
  }
  if (panel == "worldmap") {
    // Navigation aid: where you are, what is around you, and the city graph
    // (fast travel itself happens at a bus stop).
    const TileMap& m = world_.map(curMap_);
    const CityDef* here = currentCity();
    const RegionProfile* rp = regions_.profile(currentCityId());
    std::string s = "{\"city\":" + json_escape(here ? here->name : m.name) +
                    ",\"region\":" + json_escape(m.regionId) +
                    ",\"instance\":" + json_escape(m.instanceType) +
                    ",\"map\":" + json_escape(m.name) +
                    ",\"economy\":{\"community\":" + json_escape(rp ? rp->community : std::string("")) +
                    ",\"jobFocus\":" + json_escape(rp ? rp->jobFocus : std::string("")) +
                    ",\"livingCost\":" + std::to_string(rp ? rp->livingCost : 0) +
                    ",\"marketBias\":" + std::to_string(rp ? rp->marketBias : 0) + "},\"pois\":[";
    bool first = true;
    for (const auto& b : m.buildings) {
      if (!first) s += ",";
      first = false;
      s += "{\"name\":" + json_escape(b.name) + ",\"kind\":\"building\",\"x\":" +
           std::to_string((int)b.rect.x) + ",\"y\":" + std::to_string((int)b.rect.y) + "}";
    }
    for (const auto& it : m.interactables) {
      if (it.kind == "door" || it.kind == "computer") continue;
      if (!first) s += ",";
      first = false;
      s += "{\"name\":" + json_escape(it.label) + ",\"kind\":" + json_escape(it.kind) +
           ",\"x\":" + std::to_string((int)(it.rect.x / m.tile)) +
           ",\"y\":" + std::to_string((int)(it.rect.y / m.tile)) + "}";
    }
    s += "],\"cities\":[";
    first = true;
    for (const auto& c : graph_.cities()) {
      if (!first) s += ",";
      first = false;
      bool isHere = here && c.id == here->id;
      bool open = graph_.destinationUnlocked(c, player_.levels.level, player_.missions);
      const RegionProfile* rp = regions_.profile(c.id);
      s += "{\"id\":" + json_escape(c.id) + ",\"name\":" + json_escape(c.name) +
           ",\"region\":" + json_escape(c.region) +
           ",\"community\":" + json_escape(rp ? rp->community : std::string("")) +
           ",\"jobFocus\":" + json_escape(rp ? rp->jobFocus : std::string("")) +
           ",\"livingCost\":" + std::to_string(rp ? rp->livingCost : 0) +
           ",\"marketBias\":" + std::to_string(rp ? rp->marketBias : 0) +
           ",\"here\":" + (isHere ? "true" : "false") +
           ",\"unlocked\":" + (open || isHere ? "true" : "false") +
           ",\"level\":" + std::to_string(c.minLevel) +
           ",\"blurb\":" + json_escape(c.blurb) + "}";
    }
    // Dungeons you have discovered on this map (secrets stay hidden).
    s += "],\"dungeons\":[";
    first = true;
    for (const auto& d : dungeons_.all()) {
      if (d.entranceMap != curMap_) continue;
      if (!dungeons_.revealed(d, player_.flags)) continue;
      if (!first) s += ",";
      first = false;
      bool ok = dungeons_.canEnter(d, player_.levels.level, player_.flags);
      bool cleared = player_.flags.count(Dungeons::doneFlag(d.id)) > 0;
      s += "{\"id\":" + json_escape(d.id) + ",\"name\":" + json_escape(d.name) +
           ",\"blurb\":" + json_escape(d.blurb) +
           ",\"minLevel\":" + std::to_string(d.minLevel) +
           ",\"cleared\":" + (cleared ? "true" : "false") +
           ",\"enterable\":" + (ok ? "true" : "false") +
           ",\"reason\":" + json_escape(dungeons_.lockReason(d, player_.levels.level, player_.flags)) + "}";
    }
    return s + "]}";
  }
  if (panel == "skilltree") {
    int64_t need = xpForLevel(player_.levels.level);
    return "{\"level\":" + std::to_string(player_.levels.level) +
           ",\"title\":" + json_escape(titleForLevel(player_.levels.level)) +
           ",\"xp\":" + std::to_string(player_.levels.xp) +
           ",\"need\":" + std::to_string(need) +
           ",\"reputation\":" + std::to_string(player_.reputation) + "}";
  }
  if (panel == "hudstate") {
    int64_t need = xpForLevel(player_.levels.level);
    int active = countActiveMissions(player_.missions);
    int hh = (int)(dayT_ * 24), mm = (int)((dayT_ * 24 - hh) * 60);
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", hh, mm);
    std::string trackedTitle, trackedObjective;
    auto ti = missions_.find(player_.trackedMission);
    if (ti != missions_.end()) {
      trackedTitle = ti->second.title;
      auto pi = player_.missions.find(player_.trackedMission);
      if (pi != player_.missions.end()) trackedObjective = objectiveFor(ti->second, pi->second);
    }
    return "{\"name\":" + json_escape(player_.displayName) +
           ",\"level\":" + std::to_string(player_.levels.level) +
           ",\"title\":" + json_escape(titleForLevel(player_.levels.level)) +
           ",\"xp\":" + std::to_string(player_.levels.xp) +
           ",\"need\":" + std::to_string(need) +
           ",\"credits\":" + std::to_string(player_.balance()) +
           ",\"quests\":" + std::to_string(active) + ",\"clock\":" + json_escape(clock) +
           ",\"map\":" + json_escape(world_.map(curMap_).name) +
           ",\"trackedTitle\":" + json_escape(trackedTitle) +
           ",\"trackedObjective\":" + json_escape(trackedObjective) +
           ",\"reputation\":" + std::to_string(player_.reputation) +
           ",\"reputationTier\":" + json_escape(reputationTier(player_.reputation)) +
           ",\"uiScale\":" + std::to_string(player_.a11y.uiScale) +
           ",\"textScale\":" + std::to_string(player_.a11y.textScale) +
           ",\"reducedMotion\":" + (player_.a11y.reducedMotion ? "true" : "false") +
           ",\"colorblind\":" + (player_.a11y.colorblindPalette ? "true" : "false") +
           ",\"highContrast\":" + (player_.a11y.highContrast ? "true" : "false") + "}";
  }
  return "{}";
}

// ---- world / open-world systems (2.3) ----

bool Game::hatchRevealed(const Interactable& it) const {
  if (it.kind != "hatch") return true;
  for (const auto& d : dungeons_.all())
    if (d.entranceMap == curMap_ && d.interiorMap == it.target)
      return dungeons_.revealed(d, player_.flags);
  return false;
}

const DungeonDef* Game::dungeonForInterior(const std::string& mapId) const {
  for (const auto& d : dungeons_.all())
    if (d.interiorMap == mapId) return &d;
  return nullptr;
}

void Game::grantDungeonReward(const DungeonDef& d) {
  player_.flags[Dungeons::doneFlag(d.id)] = "1";
  if (!d.rewardItem.empty()) {
    player_.inventory[d.rewardItem]++;
    auto ci = catalog_.find(d.rewardItem);
    notice("Rare technology recovered: " +
               (ci != catalog_.end() ? ci->second.name : d.rewardItem) +
               (d.rewardText.empty() ? "" : " (" + d.rewardText + ")"),
           "success");
    sfx("levelup");
  }
  std::string ach = "cleared_" + d.id;
  if (std::find(player_.achievements.begin(), player_.achievements.end(), ach) ==
      player_.achievements.end())
    player_.achievements.push_back(ach);
}

void Game::revealDungeon(const std::string& flag) {
  if (flag.empty()) return;
  auto fi = player_.flags.find(flag);
  if (fi != player_.flags.end() && fi->second == "1") return;  // already known
  player_.flags[flag] = "1";
  for (const auto& d : dungeons_.all()) {
    if (d.revealBy == flag || Dungeons::revealFlag(d.id) == flag) {
      notice("You found a hidden entrance: " + d.name + ".", "quest");
      sfx("open");
    }
  }
  saveNow();
}

std::string Game::portfolioJson() {
  std::vector<PortfolioEntry> entries = Portfolio::build(missions_, player_.missions);
  int score = Portfolio::evidenceScore(entries);
  std::string s = "{\"evidence\":" + std::to_string(score) +
                  ",\"tier\":" + json_escape(Portfolio::evidenceTier(score)) +
                  ",\"completed\":" + std::to_string(Portfolio::completedCount(entries)) +
                  ",\"reputation\":" + std::to_string(player_.reputation) +
                  ",\"reputationTier\":" + json_escape(reputationTier(player_.reputation)) +
                  ",\"entries\":[";
  bool first = true;
  for (const auto& e : entries) {
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(e.id) + ",\"title\":" + json_escape(e.title) +
         ",\"language\":" + json_escape(e.language) +
         ",\"giver\":" + json_escape(e.giver) +
         ",\"level\":" + std::to_string(e.level) +
         ",\"tests\":" + std::to_string(e.tests) +
         ",\"xp\":" + std::to_string(e.xp) +
         ",\"gate\":" + (e.gate ? "true" : "false") +
         ",\"completed\":" + (e.completed ? "true" : "false") + "}";
  }
  return s + "]}";
}

std::string Game::companyJson() {
  CompanyInfo ci = Company::info(player_);
  std::string s = "{\"founded\":" + std::string(ci.founded ? "true" : "false") +
                  ",\"name\":" + json_escape(ci.name) +
                  ",\"focus\":" + json_escape(ci.focus) +
                  ",\"focusBlurb\":" + json_escape(ci.focusBlurb) +
                  ",\"rank\":" + json_escape(ci.rank) +
                  ",\"reputation\":" + std::to_string(ci.reputation) +
                  ",\"contracts\":" + std::to_string(ci.contracts) +
                  ",\"memberCount\":" + std::to_string(ci.members) +
                  ",\"focuses\":[";
  bool first = true;
  for (const auto& f : Company::focuses()) {
    if (!first) s += ",";
    first = false;
    s += json_escape(f);
  }
  s += "],\"roster\":[";
  first = true;
  for (const auto& c : social_.contacts()) {
    bool hired = std::find(player_.company.members.begin(), player_.company.members.end(),
                           c.id) != player_.company.members.end();
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(c.id) + ",\"name\":" + json_escape(c.name) +
         ",\"role\":" + json_escape(c.role) + ",\"skill\":" + json_escape(c.skill) +
         ",\"hired\":" + (hired ? "true" : "false") + "}";
  }
  // A sample contract payout so the dashboard shows the bonus it can earn.
  s += "],\"samplePayout\":" +
       std::to_string(Company::contractPayout(500, ci.reputation, ci.members)) + "}";
  return s;
}

std::string Game::contactsJson(const std::string& contactId) {
  std::string s = "{\"totalUnread\":" + std::to_string(Social::unreadCount(player_)) +
                  ",\"teamMax\":" + std::to_string(Social::kMaxTeam) +
                  ",\"active\":" + json_escape(contactId) + ",\"contacts\":[";
  bool first = true;
  for (const auto& c : social_.contacts()) {
    bool onTeam = std::find(player_.team.begin(), player_.team.end(), c.id) != player_.team.end();
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(c.id) + ",\"name\":" + json_escape(c.name) +
         ",\"role\":" + json_escape(c.role) + ",\"city\":" + json_escape(c.cityId) +
         ",\"skill\":" + json_escape(c.skill) + ",\"blurb\":" + json_escape(c.blurb) +
         ",\"unread\":" + std::to_string(Social::unreadFrom(player_, c.id)) +
         ",\"onTeam\":" + (onTeam ? "true" : "false") + "}";
  }
  s += "],\"thread\":[";
  first = true;
  for (const auto& m : Social::thread(player_, contactId)) {
    if (!first) s += ",";
    first = false;
    s += "{\"theirs\":" + std::string(m.fromThem ? "true" : "false") +
         ",\"text\":" + json_escape(m.text) + ",\"at\":" + json_escape(m.at) + "}";
  }
  return s + "]}";
}

std::string Game::homeJson() {
  const HomeDef* home = housing_.home(curMap_);
  if (!home && !housing_.all().empty()) home = &housing_.all().front();
  std::string s = "{\"map\":" + json_escape(home ? home->mapId : std::string("")) +
                  ",\"mapName\":" + json_escape(world_.map(curMap_).name) +
                  ",\"here\":" + (housing_.home(curMap_) ? "true" : "false") +
                  ",\"slots\":[";
  bool first = true;
  if (home) {
    for (const auto& sl : home->slots) {
      std::string itemId = Housing::placedIn(player_, sl.id);
      auto ci = catalog_.find(itemId);
      if (!first) s += ",";
      first = false;
      s += "{\"id\":" + json_escape(sl.id) + ",\"kind\":" + json_escape(sl.kind) +
           ",\"item\":" + json_escape(itemId) +
           ",\"name\":" + json_escape(ci != catalog_.end() ? ci->second.name : std::string("")) +
           ",\"empty\":" + (itemId.empty() ? "true" : "false") + ",\"options\":[";
      bool f2 = true;
      if (sl.kind != "trophy") {
        for (const auto& c : catalog_) {
          if (!Housing::accepts(sl, c.second)) continue;
          auto oi = player_.inventory.find(c.first);
          if (oi == player_.inventory.end() || oi->second <= 0) continue;
          if (!f2) s += ",";
          f2 = false;
          s += "{\"id\":" + json_escape(c.first) + ",\"name\":" + json_escape(c.second.name) + "}";
        }
      }
      s += "]}";
    }
  }
  HomeBenefits b = home ? Housing::benefits(player_, *home, catalog_) : HomeBenefits{};
  s += "],\"benefits\":{\"labTier\":" + std::to_string(b.labTier) +
       ",\"buildSpeed\":" + std::to_string(b.buildSpeedPercent) +
       ",\"comfort\":" + std::to_string(b.comfort) +
       ",\"trophies\":" + std::to_string(b.trophies) + "},\"trophies\":[";
  first = true;
  for (const auto& a : player_.achievements) {
    bool shown = std::find(player_.homeTrophies.begin(), player_.homeTrophies.end(), a) !=
                 player_.homeTrophies.end();
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + json_escape(a) + ",\"shown\":" + (shown ? "true" : "false") + "}";
  }
  return s + "]}";
}

void Game::placeHome(const std::string& slot, const std::string& item) {
  const HomeDef* home = housing_.home(curMap_);
  if (!home && !housing_.all().empty()) home = &housing_.all().front();
  if (!home) return;
  PlaceResult r = Housing::place(player_, *home, slot, item, catalog_);
  if (!r.ok) { notice(r.reason, "error"); sfx("error"); return; }
  auto ci = catalog_.find(item);
  notice("Placed " + (ci != catalog_.end() ? ci->second.name : item) + ".", "success");
  sfx("success");
  saveNow();
}

void Game::clearHome(const std::string& slot) {
  const HomeDef* home = housing_.home(curMap_);
  if (!home && !housing_.all().empty()) home = &housing_.all().front();
  if (!home) return;
  PlaceResult r = Housing::clear(player_, *home, slot);
  if (!r.ok) { notice(r.reason, "error"); return; }
  saveNow();
}

void Game::toggleTrophy(const std::string& achievement) {
  bool shown = std::find(player_.homeTrophies.begin(), player_.homeTrophies.end(), achievement) !=
               player_.homeTrophies.end();
  PlaceResult r = shown ? Housing::hideTrophy(player_, achievement)
                        : Housing::displayTrophy(player_, achievement);
  if (!r.ok) { notice(r.reason, "error"); sfx("error"); return; }
  notice(shown ? "Trophy taken down." : "Trophy on display.", "info");
  saveNow();
}

void Game::sendMessage(const std::string& contactId, const std::string& text) {
  const ContactDef* c = social_.contact(contactId);
  if (!c) { notice("No such contact.", "error"); return; }
  std::string msg = text;
  // Trim surrounding whitespace; empty messages are not sent.
  size_t a = msg.find_first_not_of(" \t\r\n");
  size_t b = msg.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return;
  msg = msg.substr(a, b - a + 1);
  if (msg.size() > 280) msg = msg.substr(0, 280);
  Social::exchange(player_, *c, msg, iso_now());
  sfx("interact");
  saveNow();
}

void Game::teamChange(const std::string& contactId, bool add) {
  if (add) {
    std::string why;
    if (!Social::addToTeam(player_, contactId, &why)) { notice(why, "error"); sfx("error"); return; }
    notice("Added to your team.", "success");
    sfx("success");
  } else {
    if (!Social::removeFromTeam(player_, contactId)) return;
    notice("Removed from your team.", "info");
  }
  saveNow();
}

void Game::hireContact(const std::string& contactId, bool add) {
  auto& members = player_.company.members;
  auto it = std::find(members.begin(), members.end(), contactId);
  if (add) {
    if (it == members.end()) members.push_back(contactId);
  } else if (it != members.end()) {
    members.erase(it);
  }
  saveNow();
}

void Game::foundCompany(const std::string& name, const std::string& focus) {
  std::string why;
  if (!Company::found(player_, name, focus, &why)) {
    notice(why, "error");
    sfx("error");
    return;
  }
  // Founders start with your current team on the payroll.
  for (const auto& t : player_.team) player_.company.members.push_back(t);
  notice("Company founded: " + player_.company.name + " (" + focus + ").", "success");
  sfx("levelup");
  saveNow();
}

}  // namespace dw
