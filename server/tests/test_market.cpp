#include "doctest.h"

#include "json.h"
#include "kv.h"
#include "market.h"

using namespace dw;

namespace {

const char* kMarket = R"({
  "regions": [
    {"id": "starter_town", "name": "Starter Town", "livingCost": 0, "marketBias": 0},
    {"id": "startup_city", "name": "Startup City", "livingCost": 25, "marketBias": 20},
    {"id": "systems_district", "name": "Systems District", "livingCost": 60, "marketBias": -15}
  ],
  "goods": [
    {"id": "cable-kit", "name": "Cable Kit", "category": "component", "basePrice": 8},
    {"id": "gpu-rtx", "name": "RTX GPU", "category": "component", "basePrice": 250},
    {"id": "old-phone", "name": "Old Phone", "category": "equipment", "basePrice": 30, "status": "discontinued", "replacedBy": "new-phone"},
    {"id": "new-phone", "name": "New Phone", "category": "equipment", "basePrice": 60}
  ]
})";

}  // namespace

TEST_CASE("market catalog loads and prices follow bias and demand") {
  MarketCatalog cat;
  std::string err;
  REQUIRE(cat.loadFromJson(parseJson(kMarket), &err));
  const MarketGood* cable = cat.good("cable-kit");
  REQUIRE(cable != nullptr);
  const MarketRegion* starter = cat.region("starter_town");
  const MarketRegion* startup = cat.region("startup_city");
  const MarketRegion* systems = cat.region("systems_district");
  REQUIRE(startup != nullptr);
  REQUIRE(systems != nullptr);
  // Base price, no bias, no demand.
  CHECK(MarketCatalog::priceFor(*cable, *starter, 0) == 8);
  // +20% region bias: 8 * 1.2 = 9.6, rounded to 10.
  CHECK(MarketCatalog::priceFor(*cable, *startup, 0) == 10);
  // Demand 10 adds 50%: 9.6 * 1.5 = 14.4, rounded to 14.
  CHECK(MarketCatalog::priceFor(*cable, *startup, 10) == 14);
  // Demand factor caps at 3x the region-adjusted base: 9.6 * 3 = 28.8 -> 29.
  CHECK(MarketCatalog::priceFor(*cable, *startup, 1000) == 29);
  // Negative bias: 8 * 0.85 = 6.8, rounded to 7.
  CHECK(MarketCatalog::priceFor(*cable, *systems, 0) == 7);
  // Discontinued goods parse with their replacement.
  const MarketGood* old = cat.good("old-phone");
  REQUIRE(old != nullptr);
  CHECK(old->discontinued == true);
  CHECK(old->replacedBy == "new-phone");
}

TEST_CASE("market catalog rejects bad data") {
  MarketCatalog cat;
  std::string err;
  CHECK(!cat.loadFromJson(
      parseJson(R"({"regions": [], "goods": [{"id": "x", "name": "X", "basePrice": 0}]})"),
      &err));
  CHECK(!cat.loadFromJson(
      parseJson(R"({"regions": [], "goods": [{"id": "x", "name": "X", "basePrice": 5}, {"id": "x", "name": "Y", "basePrice": 5}]})"),
      &err));
  CHECK(!cat.loadFromJson(parseJson(R"({"goods": []})"), &err));
}

TEST_CASE("market store tracks demand and inventory") {
  MemoryKv kv;
  MarketStore store(&kv);
  std::string err;
  int64_t d = -1;
  REQUIRE(store.demand("cable-kit", &d, &err));
  CHECK(d == 0);
  REQUIRE(store.addDemand("cable-kit", 3, &err));
  REQUIRE(store.demand("cable-kit", &d, &err));
  CHECK(d == 3);
  // Restock clamps at zero, never negative.
  REQUIRE(store.addDemand("cable-kit", -10, &err));
  REQUIRE(store.demand("cable-kit", &d, &err));
  CHECK(d == 0);
  // Inventory merges, then drops to zero and disappears.
  std::vector<InventoryItem> items;
  REQUIRE(store.addInventory("player-1", "cable-kit", 2, &err));
  REQUIRE(store.addInventory("player-1", "cable-kit", 3, &err));
  REQUIRE(store.inventory("player-1", &items, &err));
  REQUIRE(items.size() == 1);
  CHECK(items[0].goodId == "cable-kit");
  CHECK(items[0].qty == 5);
  REQUIRE(store.addInventory("player-1", "cable-kit", -5, &err));
  REQUIRE(store.inventory("player-1", &items, &err));
  CHECK(items.empty());
}
