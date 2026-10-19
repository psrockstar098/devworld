#pragma once
// housing.h — customizable developer homes (design 2.3: decorate apartments,
// install workstations, build home labs, display achievements).
//
// A home is an interior map with typed slots. Place decor, drop in a computer
// to make a workstation (home lab), and hang trophies from your achievements.
// Placing is pure state; the derived benefits (build speed, comfort, lab tier)
// are convenience only, matching the equipment rules.
//
// No SDL, no I/O. Content lives in content/housing.json.

#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct HomeSlot {
  std::string id;
  std::string kind;   // "workstation" | "decor" | "trophy"
  int x = 0, y = 0;   // tile coords, for rendering
};

struct HomeDef {
  std::string mapId, name;
  std::vector<HomeSlot> slots;
};

struct HomeBenefits {
  int buildSpeedPercent = 0;  // from a placed workstation
  int comfort = 0;            // from placed decor
  int labTier = 0;            // 1 when any workstation is installed (home lab)
  int trophies = 0;           // achievements on display
};

struct PlaceResult {
  bool ok = false;
  std::string reason;
};

class Housing {
 public:
  void load(const JVal& j);
  const HomeDef* home(const std::string& mapId) const;
  const std::vector<HomeDef>& all() const { return homes_; }

  // Slot lookup inside one home (nullptr if not found).
  static const HomeSlot* slot(const HomeDef& h, const std::string& slotId);

  // A slot accepts an item when the kinds line up and the item is in a concrete
  // matching slot type ("computer" -> workstation, "decor" -> decor).
  static bool accepts(const HomeSlot& s, const ItemDef& item);

  // Place an owned item into a slot (moves it from wherever it was).
  static PlaceResult place(Player& player, const HomeDef& home, const std::string& slotId,
                           const std::string& itemId,
                           const std::map<std::string, ItemDef>& catalog);
  static PlaceResult clear(Player& player, const HomeDef& home, const std::string& slotId);

  // Hang an achievement (trophy) or take it down.
  static PlaceResult displayTrophy(Player& player, const std::string& achievement);
  static PlaceResult hideTrophy(Player& player, const std::string& achievement);

  static HomeBenefits benefits(const Player& player, const HomeDef& home,
                               const std::map<std::string, ItemDef>& catalog);

  static std::string placedIn(const Player& player, const std::string& slotId);

 private:
  std::vector<HomeDef> homes_;
};

}  // namespace dw
