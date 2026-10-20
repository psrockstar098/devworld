#include "presence.h"

#include "util.h"

namespace dw {

JVal PresenceEntry::toJson() const {
  JVal j = jobj();
  // `id` is the public identity used by the game and friend lists; `playerId`
  // is kept for protocol compatibility with earlier clients.
  jset(j, "id", jstr(playerId));
  jset(j, "playerId", jstr(playerId));
  jset(j, "handle", jstr(handle));
  jset(j, "displayName", jstr(displayName));
  jset(j, "appearance", appearance.type == JVal::OBJ ? appearance : jobj());
  jset(j, "homeUnit", jnum(homeUnit));
  jset(j, "x", jnum(x));
  jset(j, "y", jnum(y));
  jset(j, "mapId", jstr(mapId));
  jset(j, "updatedAt", jnum((double)updatedAt));
  return j;
}

void PresenceStore::update(const PresenceEntry& e) {
  PresenceEntry copy = e;
  copy.updatedAt = nowMs();
  {
    std::lock_guard<std::mutex> lock(mu_);
    map_[copy.playerId] = copy;
  }
  if (cache_) {
    std::string err;
    cache_->set("presence:" + copy.playerId, dumpJson(copy.toJson()), kTtlSec, &err);
  }
}

void PresenceStore::remove(const std::string& playerId) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    map_.erase(playerId);
  }
  if (cache_) {
    std::string err;
    cache_->del("presence:" + playerId, &err);
  }
}

std::vector<PresenceEntry> PresenceStore::list(int64_t maxAgeMs) {
  int64_t cutoff = nowMs() - maxAgeMs;
  std::vector<PresenceEntry> out;
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = map_.begin(); it != map_.end();) {
    if (it->second.updatedAt < cutoff) {
      it = map_.erase(it);
    } else {
      out.push_back(it->second);
      ++it;
    }
  }
  return out;
}

bool PresenceStore::isOnline(const std::string& playerId) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = map_.find(playerId);
  return it != map_.end() && nowMs() - it->second.updatedAt <= 30000;
}

}  // namespace dw
