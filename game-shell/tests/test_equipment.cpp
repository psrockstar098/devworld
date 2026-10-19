// tests for src/equipment.h — slots, equip/unequip rules and derived bonuses.
#include "doctest.h"
#include "equipment.h"

using namespace dw;

static std::map<std::string, ItemDef> catalog() {
  std::map<std::string, ItemDef> c;
  ItemDef laptop; laptop.id = "laptop_pro"; laptop.name = "Pro Laptop"; laptop.slot = "computer";
  laptop.effects.concurrentMissions = 2; laptop.effects.buildSpeedPercent = 25;
  ItemDef sheets; sheets.id = "cheatsheets"; sheets.name = "Cheat Sheets"; sheets.slot = "tool";
  sheets.effects.xpPercent = 10;
  ItemDef chair; chair.id = "chair"; chair.name = "Ergo Chair"; chair.slot = "decor";
  chair.effects.comfort = 10;
  ItemDef noodles; noodles.id = "noodles"; noodles.name = "Noodles"; noodles.slot = "consumable";
  c["laptop_pro"] = laptop; c["cheatsheets"] = sheets; c["chair"] = chair; c["noodles"] = noodles;
  return c;
}

static Player owner(const std::string& id, int qty = 1) {
  Player p; p.inventory[id] = qty; return p;
}

TEST_CASE("slots are exactly computer, tool and decor") {
  const auto& s = Equipment::slots();
  CHECK(s.size() == 3);
  CHECK(Equipment::isSlot("computer"));
  CHECK(Equipment::isSlot("tool"));
  CHECK(Equipment::isSlot("decor"));
  CHECK_FALSE(Equipment::isSlot("consumable"));
  CHECK_FALSE(Equipment::isSlot(""));
  CHECK_FALSE(Equipment::isSlot("hat"));
}

TEST_CASE("you cannot equip what you do not own") {
  auto c = catalog();
  Player p;  // empty pockets
  EquipResult r = Equipment::equip(p, "cheatsheets", c);
  CHECK_FALSE(r.ok);
  CHECK(r.reason.find("do not own") != std::string::npos);
  CHECK(p.equipped.empty());
}

TEST_CASE("consumables and unknown items are rejected") {
  auto c = catalog();
  Player p = owner("noodles");
  CHECK_FALSE(Equipment::equip(p, "noodles", c).ok);     // not an equippable slot
  CHECK_FALSE(Equipment::equip(p, "ghost_item", c).ok);  // not in the catalog
  CHECK(p.equipped.count("tool") == 0);
}

TEST_CASE("equipping fills the right slot and unequip clears it") {
  auto c = catalog();
  Player p = owner("cheatsheets");
  p.inventory["chair"] = 1;

  REQUIRE(Equipment::equip(p, "cheatsheets", c).ok);
  CHECK(Equipment::equippedIn(p, "tool") == "cheatsheets");
  REQUIRE(Equipment::equip(p, "chair", c).ok);
  CHECK(Equipment::equippedIn(p, "decor") == "chair");

  CHECK(Equipment::unequip(p, "tool").ok);
  CHECK(Equipment::equippedIn(p, "tool").empty());
  CHECK(Equipment::equippedIn(p, "decor") == "chair");  // untouched
}

TEST_CASE("a second item in the same slot replaces the first") {
  auto c = catalog();
  ItemDef sheets2; sheets2.id = "cards"; sheets2.name = "Flash Cards"; sheets2.slot = "tool";
  c["cards"] = sheets2;
  Player p = owner("cheatsheets"); p.inventory["cards"] = 1;

  REQUIRE(Equipment::equip(p, "cheatsheets", c).ok);
  REQUIRE(Equipment::equip(p, "cards", c).ok);
  CHECK(Equipment::equippedIn(p, "tool") == "cards");
  CHECK(p.equipped.size() == 1);
}

TEST_CASE("unequipping an empty or bogus slot reports why") {
  auto c = catalog();
  Player p;
  CHECK_FALSE(Equipment::unequip(p, "tool").ok);
  CHECK_FALSE(Equipment::unequip(p, "hat").ok);
}

TEST_CASE("effects sum across the load-out") {
  auto c = catalog();
  Player p = owner("cheatsheets");
  p.inventory["laptop_pro"] = 1;
  p.inventory["chair"] = 1;
  REQUIRE(Equipment::equip(p, "cheatsheets", c).ok);
  REQUIRE(Equipment::equip(p, "laptop_pro", c).ok);
  REQUIRE(Equipment::equip(p, "chair", c).ok);

  EquipEffects e = Equipment::effects(p, c);
  CHECK(e.xpPercent == 10);
  CHECK(e.extraMissionSlots == 2);
  CHECK(e.buildSpeedPercent == 25);
  CHECK(e.comfort == 10);
}

TEST_CASE("an equipped item removed from content is ignored, not crashed on") {
  auto c = catalog();
  Player p = owner("cheatsheets");
  REQUIRE(Equipment::equip(p, "cheatsheets", c).ok);
  auto without = catalog();
  without.erase("cheatsheets");
  EquipEffects e = Equipment::effects(p, without);
  CHECK(e.xpPercent == 0);
}

TEST_CASE("mission slots start at one and grow with equipment") {
  auto c = catalog();
  Player p;
  CHECK(Equipment::missionSlotLimit(p, c) == 1);
  p.inventory["laptop_pro"] = 1;
  REQUIRE(Equipment::equip(p, "laptop_pro", c).ok);
  CHECK(Equipment::missionSlotLimit(p, c) == 3);  // 1 base + 2
}
