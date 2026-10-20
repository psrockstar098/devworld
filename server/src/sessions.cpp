#include "sessions.h"

#include "util.h"

namespace dw {

bool SessionStore::create(const std::string& playerId, IssuedSession* out,
                          std::string* err) {
  std::string token = randomBase64Url(32);
  if (token.empty()) {
    if (err) *err = "could not generate session token";
    return false;
  }
  if (!cache_->set(tokenKey(token), playerId, ttlSec_, err)) return false;
  out->token = token;
  out->expiresInSec = ttlSec_;
  return true;
}

bool SessionStore::resolve(const std::string& token, std::string* playerId,
                           bool* found, std::string* err) {
  *found = false;
  if (token.empty() || token.size() > 512) return true;
  return cache_->get(tokenKey(token), playerId, found, err);
}

bool SessionStore::touch(const std::string& token, std::string* err) {
  if (token.empty()) return true;
  return cache_->expire(tokenKey(token), ttlSec_, err);
}

bool SessionStore::destroy(const std::string& token, std::string* err) {
  if (token.empty()) return true;
  return cache_->del(tokenKey(token), err);
}

}  // namespace dw
