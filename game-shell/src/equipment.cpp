#include "equipment.h"

#include <algorithm>

namespace dw {

const std::vector<std::string>& Equipment::slots() {
  static const std::vector<std::string> s{"computer", "tool", "decor"};
  return s;
}

bool Equipment::isSlot(const std::string& slot) {
  const auto& s = slots();
  return std::find(s.begin(), s.end(), slot) != s.end();
}

bool Equipment::equippable(const ItemDef& item) { return isSlot(item.slot); }

EquipResult Equipment::equip(Player& player, const std::string& itemId,
                             const std::map<std::string, ItemDef>& catalog) {
  auto ci = catalog.find(itemId);
  if (ci == catalog.end()) return {false, "No such item."};
  const ItemDef& item = ci->second;
  if (!equippable(item)) return {false, item.name + " cannot be equipped."};

  auto oi = player.inventory.find(itemId);
  if (oi == player.inventory.end() || oi->second <= 0)
    return {false, "You do not own " + item.name + "."};

  player.equipped[item.slot] = itemId;
  return {true, ""};
}

EquipResult Equipment::unequip(Player& player, const std::string& slot) {
  if (!isSlot(slot)) return {false, "Nothing goes there."};
  auto it = player.equipped.find(slot);
  if (it == player.equipped.end()) return {false, "Nothing equipped in that slot."};
  player.equipped.erase(it);
  return {true, ""};
}

EquipEffects Equipment::effects(const Player& player,
                                const std::map<std::string, ItemDef>& catalog) {
  EquipEffects e;
  for (const auto& kv : player.equipped) {
    auto ci = catalog.find(kv.second);
    if (ci == catalog.end()) continue;  // item removed from content
    e.xpPercent += ci->second.effects.xpPercent;
    e.extraMissionSlots += ci->second.effects.concurrentMissions;
    e.buildSpeedPercent += ci->second.effects.buildSpeedPercent;
    e.comfort += ci->second.effects.comfort;
  }
  return e;
}

int Equipment::missionSlotLimit(const Player& player,
                                const std::map<std::string, ItemDef>& catalog) {
  return 1 + std::max(0, effects(player, catalog).extraMissionSlots);
}

std::string Equipment::equippedIn(const Player& player, const std::string& slot) {
  auto it = player.equipped.find(slot);
  return it == player.equipped.end() ? "" : it->second;
}

}  // namespace dw
