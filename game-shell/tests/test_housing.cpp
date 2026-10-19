// tests for src/housing.h — customizable developer homes: placement and benefits.
#include "doctest.h"
#include "housing.h"
#include "json.h"

using namespace dw;

static HomeDef home() {
  HomeDef h;
  h.mapId = "apt_interior";
  h.name = "Your Apartment";
  h.slots = {{"desk", "workstation", 2, 3},
             {"shelf", "decor", 11, 2},
             {"trophy_wall", "trophy", 6, 8}};
  return h;
}

static std::map<std::string, ItemDef> catalog() {
  std::map<std::string, ItemDef> c;
  ItemDef pc; pc.id = "rig"; pc.name = "Tower Rig"; pc.slot = "computer";
  pc.effects.buildSpeedPercent = 30;
  ItemDef plant; plant.id = "plant"; plant.name = "Desk Plant"; plant.slot = "decor";
  plant.effects.comfort = 8;
  ItemDef lamp; lamp.id = "lamp"; lamp.name = "Warm Lamp"; lamp.slot = "decor";
  lamp.effects.comfort = 4;
  ItemDef coffee; coffee.id = "coffee"; coffee.name = "Cold Brew"; coffee.slot = "consumable";
  c["rig"] = pc; c["plant"] = plant; c["lamp"] = lamp; c["coffee"] = coffee;
  return c;
}

TEST_CASE("a home loads with typed slots") {
  Housing hs;
  hs.load(parseJson(R"({"homes":[{"mapId":"apt_interior","name":"Your Apartment",
    "slots":[{"id":"desk","kind":"workstation","x":2,"y":3},
             {"id":"shelf","kind":"decor","x":11,"y":2}]}]})"));
  const HomeDef* h = hs.home("apt_interior");
  REQUIRE(h != nullptr);
  CHECK(h->slots.size() == 2);
  CHECK(Housing::slot(*h, "desk")->kind == "workstation");
  CHECK(Housing::slot(*h, "ghost") == nullptr);
  CHECK(hs.home("nowhere") == nullptr);
}

TEST_CASE("slot kinds accept the right items") {
  HomeDef h = home();
  auto c = catalog();
  CHECK(Housing::accepts(*Housing::slot(h, "desk"), c.at("rig")));      // computer -> workstation
  CHECK(Housing::accepts(*Housing::slot(h, "shelf"), c.at("plant")));   // decor -> decor
  CHECK_FALSE(Housing::accepts(*Housing::slot(h, "desk"), c.at("plant")));
  CHECK_FALSE(Housing::accepts(*Housing::slot(h, "shelf"), c.at("rig")));
  CHECK_FALSE(Housing::accepts(*Housing::slot(h, "trophy_wall"), c.at("plant")));  // trophies are not items
}

TEST_CASE("you cannot place what you do not own, or into the wrong slot") {
  HomeDef h = home();
  auto c = catalog();
  Player p;  // empty pockets
  CHECK_FALSE(Housing::place(p, h, "desk", "rig", c).ok);
  p.inventory["plant"] = 1;
  CHECK_FALSE(Housing::place(p, h, "desk", "plant", c).ok);   // decor into workstation
  CHECK_FALSE(Housing::place(p, h, "desk", "ghost", c).ok);   // unknown item
  CHECK_FALSE(Housing::place(p, h, "ghost", "plant", c).ok);  // unknown slot
  CHECK(p.homeLayout.empty());
}

TEST_CASE("placing and clearing decor works") {
  HomeDef h = home();
  auto c = catalog();
  Player p; p.inventory["plant"] = 2; p.inventory["rig"] = 1;

  REQUIRE(Housing::place(p, h, "shelf", "plant", c).ok);
  CHECK(Housing::placedIn(p, "shelf") == "plant");
  CHECK(Housing::clear(p, h, "shelf").ok);
  CHECK(Housing::placedIn(p, "shelf").empty());
  CHECK_FALSE(Housing::clear(p, h, "shelf").ok);  // already empty
}

TEST_CASE("an item lives in exactly one home slot") {
  HomeDef h = home();
  auto c = catalog();
  Player p; p.inventory["lamp"] = 1;
  REQUIRE(Housing::place(p, h, "shelf", "lamp", c).ok);
  // Same slot twice is idempotent; a different slot moves it.
  REQUIRE(Housing::place(p, h, "shelf", "lamp", c).ok);
  CHECK(p.homeLayout.size() == 1);
}

TEST_CASE("home benefits come from placed items, not the whole bag") {
  HomeDef h = home();
  auto c = catalog();
  Player p; p.inventory["rig"] = 1; p.inventory["plant"] = 1; p.inventory["coffee"] = 1;

  HomeBenefits empty = Housing::benefits(p, h, c);
  CHECK(empty.labTier == 0);
  CHECK(empty.buildSpeedPercent == 0);

  REQUIRE(Housing::place(p, h, "desk", "rig", c).ok);
  REQUIRE(Housing::place(p, h, "shelf", "plant", c).ok);
  HomeBenefits b = Housing::benefits(p, h, c);
  CHECK(b.labTier == 1);
  CHECK(b.buildSpeedPercent == 30);
  CHECK(b.comfort == 8);
}

TEST_CASE("trophies are earned achievements, never invented") {
  HomeDef h = home();
  Player p;
  CHECK_FALSE(Housing::displayTrophy(p, "first_deploy").ok);  // not earned
  p.achievements.push_back("first_deploy");
  REQUIRE(Housing::displayTrophy(p, "first_deploy").ok);
  CHECK(p.homeTrophies.size() == 1);
  REQUIRE(Housing::displayTrophy(p, "first_deploy").ok);      // no duplicates
  CHECK(p.homeTrophies.size() == 1);
  CHECK(Housing::hideTrophy(p, "first_deploy").ok);
  CHECK(p.homeTrophies.empty());
  CHECK_FALSE(Housing::hideTrophy(p, "first_deploy").ok);
}
