#include "housing.h"

#include <algorithm>

namespace dw {

void Housing::load(const JVal& j) {
  homes_.clear();
  for (const auto& h : j.at("homes").arr) {
    HomeDef d;
    d.mapId = h.s("mapId");
    d.name = h.s("name");
    for (const auto& s : h.at("slots").arr) {
      HomeSlot sl;
      sl.id = s.s("id");
      sl.kind = s.s("kind");
      sl.x = s.i("x");
      sl.y = s.i("y");
      d.slots.push_back(sl);
    }
    homes_.push_back(d);
  }
}

const HomeDef* Housing::home(const std::string& mapId) const {
  for (const auto& h : homes_)
    if (h.mapId == mapId) return &h;
  return nullptr;
}

const HomeSlot* Housing::slot(const HomeDef& h, const std::string& slotId) {
  for (const auto& s : h.slots)
    if (s.id == slotId) return &s;
  return nullptr;
}

bool Housing::accepts(const HomeSlot& s, const ItemDef& item) {
  if (s.kind == "workstation") return item.slot == "computer";
  if (s.kind == "decor") return item.slot == "decor";
  return false;  // trophy slots hold achievements, not items
}

PlaceResult Housing::place(Player& player, const HomeDef& home, const std::string& slotId,
                           const std::string& itemId,
                           const std::map<std::string, ItemDef>& catalog) {
  const HomeSlot* s = slot(home, slotId);
  if (!s) return {false, "No such spot."};
  auto ci = catalog.find(itemId);
  if (ci == catalog.end()) return {false, "No such item."};
  if (!accepts(*s, ci->second))
    return {false, ci->second.name + " does not go there."};
  auto oi = player.inventory.find(itemId);
  if (oi == player.inventory.end() || oi->second <= 0)
    return {false, "You do not own " + ci->second.name + "."};

  // An item lives in one place: clear it from any other home slot first.
  for (auto& kv : player.homeLayout)
    if (kv.second == itemId) kv.second.clear();
  player.homeLayout[slotId] = itemId;
  return {true, ""};
}

PlaceResult Housing::clear(Player& player, const HomeDef& home, const std::string& slotId) {
  if (!slot(home, slotId)) return {false, "No such spot."};
  auto it = player.homeLayout.find(slotId);
  if (it == player.homeLayout.end() || it->second.empty())
    return {false, "Nothing is placed there."};
  it->second.clear();
  return {true, ""};
}

PlaceResult Housing::displayTrophy(Player& player, const std::string& achievement) {
  if (std::find(player.achievements.begin(), player.achievements.end(), achievement) ==
      player.achievements.end())
    return {false, "You have not earned that yet."};
  if (std::find(player.homeTrophies.begin(), player.homeTrophies.end(), achievement) ==
      player.homeTrophies.end())
    player.homeTrophies.push_back(achievement);
  return {true, ""};
}

PlaceResult Housing::hideTrophy(Player& player, const std::string& achievement) {
  auto it = std::find(player.homeTrophies.begin(), player.homeTrophies.end(), achievement);
  if (it == player.homeTrophies.end()) return {false, "That trophy is not on display."};
  player.homeTrophies.erase(it);
  return {true, ""};
}

HomeBenefits Housing::benefits(const Player& player, const HomeDef& home,
                               const std::map<std::string, ItemDef>& catalog) {
  HomeBenefits b;
  for (const auto& s : home.slots) {
    auto it = player.homeLayout.find(s.id);
    if (it == player.homeLayout.end() || it->second.empty()) continue;
    auto ci = catalog.find(it->second);
    if (ci == catalog.end()) continue;
    if (s.kind == "workstation") {
      b.labTier = 1;
      b.buildSpeedPercent += ci->second.effects.buildSpeedPercent;
    } else if (s.kind == "decor") {
      b.comfort += ci->second.effects.comfort;
    }
  }
  b.trophies = (int)player.homeTrophies.size();
  return b;
}

std::string Housing::placedIn(const Player& player, const std::string& slotId) {
  auto it = player.homeLayout.find(slotId);
  return it == player.homeLayout.end() ? "" : it->second;
}

}  // namespace dw
