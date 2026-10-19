#pragma once
// equipment.h — pure inventory/equipment rules.
//
// Items live in slots (computer, tool, decor). Equipment is a *convenience*
// layer only: it can speed up simulated builds or add mission slots, but the
// design forbids anything that changes test outcomes (PRD pillar 1). The
// effects are additive and derived — there is no stored "equipped bonus".
// See docs/design/02-game-design-document.md §6.

#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct EquipResult {
  bool ok = false;
  std::string reason;  // human text when !ok
};

// Derived convenience bonuses from everything currently equipped.
struct EquipEffects {
  int xpPercent = 0;              // extra mission XP while equipped
  int extraMissionSlots = 0;      // beyond the base of 1 concurrent mission
  int buildSpeedPercent = 0;      // simulated build speed (cosmetic toast timing)
  int comfort = 0;                // flavor stat shown in the profile
};

class Equipment {
 public:
  static const std::vector<std::string>& slots();      // equippable slots
  static bool isSlot(const std::string& slot);
  static bool equippable(const ItemDef& item);         // has a real slot

  // Equip an item the player owns. Replaces whatever occupies the slot.
  static EquipResult equip(Player& player, const std::string& itemId,
                           const std::map<std::string, ItemDef>& catalog);
  static EquipResult unequip(Player& player, const std::string& slot);

  // Summed bonuses of the current load-out (unknown items ignored).
  static EquipEffects effects(const Player& player,
                              const std::map<std::string, ItemDef>& catalog);

  // Base 1 + equipment: how many missions may be active at once.
  static int missionSlotLimit(const Player& player,
                              const std::map<std::string, ItemDef>& catalog);

  static std::string equippedIn(const Player& player, const std::string& slot);
};

}  // namespace dw
