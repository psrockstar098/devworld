#include "market.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "util.h"

namespace dw {

int64_t MarketCatalog::priceFor(const MarketGood& good,
                                 const MarketRegion& region,
                                 int64_t netDemand) {
  if (netDemand < 0) netDemand = 0;
  // Demand factor in percent: 100 + 5 per net demand, capped at 300 (3x).
  int64_t demandPct = 100 + 5 * netDemand;
  if (demandPct > 300) demandPct = 300;
  int64_t regionPct = 100 + region.marketBias;
  if (regionPct < 0) regionPct = 0;
  // Rounded to whole credits: (base * regionPct * demandPct + 5000) / 10000.
  __int128 v = (__int128)good.basePrice * regionPct * demandPct;
  int64_t price = (int64_t)((v + 5000) / 10000);
  return price < 0 ? 0 : price;
}

const MarketGood* MarketCatalog::good(const std::string& id) const {
  auto it = goods_.find(id);
  return it == goods_.end() ? nullptr : &it->second;
}

const MarketRegion* MarketCatalog::region(const std::string& id) const {
  auto it = regions_.find(id);
  return it == regions_.end() ? nullptr : &it->second;
}

bool MarketCatalog::loadFromJson(const JVal& j, std::string* err) {
  auto fail = [&](const std::string& msg) {
    if (err) *err = msg;
    return false;
  };
  if (j.type != JVal::OBJ) return fail("market catalog must be an object");
  const JVal& regions = j.at("regions");
  if (regions.type != JVal::ARR) return fail("market catalog needs a regions array");
  const JVal& goods = j.at("goods");
  if (goods.type != JVal::ARR) return fail("market catalog needs a goods array");

  std::map<std::string, MarketRegion> newRegions;
  for (const auto& r : regions.arr) {
    MarketRegion region;
    region.id = r.s("id");
    region.name = r.s("name");
    region.livingCost = (int64_t)r.n("livingCost", 0);
    region.marketBias = (int64_t)r.n("marketBias", 0);
    if (region.id.empty()) return fail("market region needs an id");
    if (newRegions.count(region.id)) return fail("duplicate market region: " + region.id);
    newRegions[region.id] = region;
  }
  std::map<std::string, MarketGood> newGoods;
  for (const auto& g : goods.arr) {
    MarketGood good;
    good.id = g.s("id");
    good.name = g.s("name");
    good.category = g.s("category");
    good.basePrice = (int64_t)g.n("basePrice", 0);
    good.discontinued = g.s("status") == "discontinued";
    good.replacedBy = g.s("replacedBy");
    if (good.id.empty()) return fail("market good needs an id");
    if (good.basePrice < 1) return fail("market good needs a basePrice >= 1: " + good.id);
    if (newGoods.count(good.id)) return fail("duplicate market good: " + good.id);
    newGoods[good.id] = good;
  }
  regions_ = std::move(newRegions);
  goods_ = std::move(newGoods);
  return true;
}

bool MarketCatalog::loadFromFile(const std::string& path, std::string* err) {
  std::ifstream in(path);
  if (!in) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  try {
    JVal j = parseJson(ss.str());
    return loadFromJson(j, err);
  } catch (const std::exception& e) {
    if (err) *err = std::string("bad market JSON: ") + e.what();
    return false;
  }
}

namespace {

std::string demandKey(const std::string& goodId) {
  return "market:demand:" + goodId;
}

std::string inventoryKey(const std::string& playerId) {
  return "market:inventory:" + playerId;
}

}  // namespace

bool MarketStore::demand(const std::string& goodId, int64_t* out,
                         std::string* err) {
  std::string raw;
  bool found = false;
  if (!kv_->get(demandKey(goodId), &raw, &found, err)) return false;
  if (!found) {
    *out = 0;
    return true;
  }
  int64_t v = 0;
  if (!parseInt64(raw, &v)) {
    if (err) *err = "corrupt market demand for " + goodId;
    return false;
  }
  *out = v < 0 ? 0 : v;
  return true;
}

bool MarketStore::addDemand(const std::string& goodId, int64_t delta,
                            std::string* err) {
  int64_t cur = 0;
  if (!demand(goodId, &cur, err)) return false;
  int64_t next = cur + delta;
  if (next < 0) next = 0;
  return kv_->set(demandKey(goodId), std::to_string(next), 0, err);
}

bool MarketStore::addInventory(const std::string& playerId,
                               const std::string& goodId, int64_t qty,
                               std::string* err) {
  std::vector<InventoryItem> items;
  if (!inventory(playerId, &items, err)) return false;
  bool seen = false;
  for (auto& it : items) {
    if (it.goodId == goodId) {
      it.qty += qty;
      seen = true;
    }
  }
  if (!seen && qty > 0) items.push_back({goodId, qty});
  std::vector<InventoryItem> kept;
  for (const auto& it : items) {
    if (it.qty > 0) kept.push_back(it);
  }
  JVal arr = jarr();
  for (const auto& it : kept) {
    JVal o = jobj();
    jset(o, "goodId", jstr(it.goodId));
    jset(o, "qty", jnum((double)it.qty));
    arr.arr.push_back(o);
  }
  return kv_->set(inventoryKey(playerId), dumpJson(arr), 0, err);
}

bool MarketStore::inventory(const std::string& playerId,
                            std::vector<InventoryItem>* out,
                            std::string* err) {
  std::string raw;
  bool found = false;
  if (!kv_->get(inventoryKey(playerId), &raw, &found, err)) return false;
  out->clear();
  if (!found || raw.empty()) return true;
  JVal j;
  try {
    j = parseJson(raw);
  } catch (...) {
    if (err) *err = "corrupt market inventory for " + playerId;
    return false;
  }
  if (j.type != JVal::ARR) {
    if (err) *err = "corrupt market inventory for " + playerId;
    return false;
  }
  for (const auto& o : j.arr) {
    InventoryItem it;
    it.goodId = o.s("goodId");
    it.qty = (int64_t)o.n("qty", 0);
    if (!it.goodId.empty() && it.qty > 0) out->push_back(it);
  }
  return true;
}

}  // namespace dw
