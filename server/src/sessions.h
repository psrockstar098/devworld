#pragma once
// sessions.h — opaque bearer tokens in Redis with a TTL (sliding on use).
#include <string>

#include "kv.h"

namespace dw {

struct IssuedSession {
  std::string token;
  int expiresInSec = 0;
};

class SessionStore {
 public:
  SessionStore(KvStore* cache, int ttlSec)
      : cache_(cache), ttlSec_(ttlSec > 0 ? ttlSec : 604800) {}

  bool create(const std::string& playerId, IssuedSession* out,
              std::string* err);
  bool resolve(const std::string& token, std::string* playerId, bool* found,
               std::string* err);
  bool touch(const std::string& token, std::string* err);
  bool destroy(const std::string& token, std::string* err);

  static std::string tokenKey(const std::string& token) {
    return "session:" + token;
  }
  int ttlSec() const { return ttlSec_; }

 private:
  KvStore* cache_;
  int ttlSec_;
};

}  // namespace dw
