#pragma once
// presence.h — who is online in the shared world, and where they are.
//
// Presence is ephemeral by nature: the authoritative copy is the live server,
// mirrored into Redis with a short TTL so a restart starts clean instead of
// showing ghosts. Positions are world pixels in the shared town; interiors are
// private, so a player in an interior is online but not visible in the street.
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

struct PresenceEntry {
  std::string playerId;
  std::string handle;
  std::string displayName;
  JVal appearance = jobj();
  int homeUnit = 0;
  double x = 0;
  double y = 0;
  std::string mapId = "town";
  int64_t updatedAt = 0;

  JVal toJson() const;
};

class PresenceStore {
 public:
  // `cache` may be null in unit tests; the in-process map still works.
  explicit PresenceStore(KvStore* cache = nullptr) : cache_(cache) {}

  void update(const PresenceEntry& e);
  void remove(const std::string& playerId);
  // Entries seen within `maxAgeMs`; stale entries are purged.
  std::vector<PresenceEntry> list(int64_t maxAgeMs = 30000);
  bool isOnline(const std::string& playerId);

  static constexpr int kTtlSec = 30;

 private:
  std::mutex mu_;
  std::map<std::string, PresenceEntry> map_;
  KvStore* cache_;
};

}  // namespace dw
