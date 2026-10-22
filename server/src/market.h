#pragma once
// market.h — server-side market: goods, demand-driven pricing, purchases.
//
// Prices are computed, never random:
//   price = base * (1 + marketBias/100) * (1 + 0.05 * netDemand), capped at 3x
//   the region-adjusted base, rounded to whole credits.
// Every purchase raises netDemand for that good (prices rise with
// popularity); an admin restock lowers it (new supply pushes prices down).
// Purchases move money through the double-entry ledger (wallet -> system),
// so wealth never appears from nothing, and every purchase and restock is
// recorded in the world event log. Demand is tracked globally per good in
// Phase 1; per-region demand is a later layer, not a silent assumption.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

struct MarketRegion {
  std::string id;
  std::string name;
  int64_t livingCost = 0;  // credits/day, informational for now
  int64_t marketBias = 0;  // percent, e.g. 20 = +20% prices
};

struct MarketGood {
  std::string id;
  std::string name;
  std::string category;
  int64_t basePrice = 0;  // credits, before region bias and demand
  bool discontinued = false;
  std::string replacedBy;  // id of the replacement good, if discontinued
};

class MarketCatalog {
 public:
  bool loadFromJson(const JVal& j, std::string* err);
  bool loadFromFile(const std::string& path, std::string* err);
  const MarketGood* good(const std::string& id) const;
  const MarketRegion* region(const std::string& id) const;
  const std::map<std::string, MarketGood>& goods() const { return goods_; }
  const std::map<std::string, MarketRegion>& regions() const { return regions_; }
  // Current price in credits for one unit, given tracked net demand.
  static int64_t priceFor(const MarketGood& good, const MarketRegion& region,
                          int64_t netDemand);

 private:
  std::map<std::string, MarketGood> goods_;
  std::map<std::string, MarketRegion> regions_;
};

struct InventoryItem {
  std::string goodId;
  int64_t qty = 0;
};

class MarketStore {
 public:
  explicit MarketStore(KvStore* kv) : kv_(kv) {}
  // Net demand for a good (purchases minus restocks, never below zero).
  // Returns false only when the store itself fails.
  bool demand(const std::string& goodId, int64_t* out, std::string* err);
  // Applies a demand delta (positive for purchases, negative for restocks),
  // clamped at zero. Returns false only when the store itself fails.
  bool addDemand(const std::string& goodId, int64_t delta, std::string* err);
  // Adjusts a player's inventory by qty (negative removes, down to zero,
  // dropping the entry when it reaches zero).
  bool addInventory(const std::string& playerId, const std::string& goodId,
                    int64_t qty, std::string* err);
  bool inventory(const std::string& playerId, std::vector<InventoryItem>* out,
                 std::string* err);

 private:
  KvStore* kv_;
};

}  // namespace dw
