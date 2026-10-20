#include "kv.h"

#include <cstdlib>

#include "util.h"

namespace dw {

bool parseRedisUrl(const std::string& url, RedisUrl* out, std::string* err) {
  *out = RedisUrl{};
  std::string rest = url;
  if (startsWith(rest, "redis://")) rest = rest.substr(8);
  else if (startsWith(rest, "rediss://")) {
    if (err) *err = "TLS redis URLs are not supported";
    return false;
  }
  size_t at = rest.rfind('@');
  if (at != std::string::npos) {
    std::string userinfo = rest.substr(0, at);
    rest = rest.substr(at + 1);
    size_t colon = userinfo.find(':');
    std::string pw = colon == std::string::npos ? userinfo
                                                : userinfo.substr(colon + 1);
    out->password = urlDecode(pw);
  }
  size_t slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    out->host = rest.substr(0, colon);
    int p = 0;
    if (!parseInt(rest.substr(colon + 1), &p) || p <= 0 || p > 65535) {
      if (err) *err = "bad redis port";
      return false;
    }
    out->port = p;
  } else {
    out->host = rest;
  }
  if (out->host.empty()) out->host = "127.0.0.1";
  return true;
}

MemoryKv::MemoryKv(std::function<int64_t()> now)
    : now_(now ? std::move(now) : [] { return nowMs(); }) {}

MemoryKv::Entry* MemoryKv::live(const std::string& key) {
  auto it = map_.find(key);
  if (it == map_.end()) return nullptr;
  if (it->second.expiresAt >= 0 && it->second.expiresAt <= now_()) {
    map_.erase(it);
    return nullptr;
  }
  return &it->second;
}

bool MemoryKv::get(const std::string& key, std::string* value, bool* found,
                   std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  Entry* e = live(key);
  if (!e) {
    if (found) *found = false;
    if (value) value->clear();
    return true;
  }
  if (found) *found = true;
  if (value) *value = e->value;
  return true;
}

bool MemoryKv::set(const std::string& key, const std::string& value, int ttlSec,
                   std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  Entry e;
  e.value = value;
  e.expiresAt = ttlSec > 0 ? now_() + (int64_t)ttlSec * 1000 : -1;
  map_[key] = std::move(e);
  return true;
}

bool MemoryKv::del(const std::string& key, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  map_.erase(key);
  return true;
}

bool MemoryKv::incr(const std::string& key, int64_t by, int64_t* out,
                    std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  Entry* e = live(key);
  int64_t n = 0;
  if (e) n = std::strtoll(e->value.c_str(), nullptr, 10);
  n += by;
  if (e) {
    e->value = std::to_string(n);
  } else {
    Entry fresh;
    fresh.value = std::to_string(n);
    map_[key] = std::move(fresh);
  }
  if (out) *out = n;
  return true;
}

bool MemoryKv::expire(const std::string& key, int ttlSec, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  Entry* e = live(key);
  if (e) e->expiresAt = ttlSec > 0 ? now_() + (int64_t)ttlSec * 1000 : -1;
  return true;
}

bool MemoryKv::ttl(const std::string& key, int* out, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  Entry* e = live(key);
  if (!e) {
    if (out) *out = -2;
    return true;
  }
  if (e->expiresAt < 0) {
    if (out) *out = -1;
    return true;
  }
  int64_t left = (e->expiresAt - now_() + 999) / 1000;
  if (out) *out = left > 0 ? (int)left : 0;
  return true;
}

bool MemoryKv::ping(std::string*) { return true; }

size_t MemoryKv::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return map_.size();
}

bool RespKv::connect(const std::string& host, int port,
                     const std::string& password, int timeoutMs,
                     std::string* err) {
  return client_.connect(host, port, password, timeoutMs, err);
}

bool RespKv::connectUrl(const std::string& url, int timeoutMs, std::string* err) {
  RedisUrl parsed;
  if (!parseRedisUrl(url, &parsed, err)) return false;
  return connect(parsed.host, parsed.port, parsed.password, timeoutMs, err);
}

void RespKv::close() { client_.close(); }

bool RespKv::get(const std::string& key, std::string* value, bool* found,
                 std::string* err) {
  RespReply r;
  if (!client_.command({"GET", key}, &r, err)) return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  if (r.isNil()) {
    if (found) *found = false;
    if (value) value->clear();
    return true;
  }
  if (found) *found = true;
  if (value) *value = r.str;
  return true;
}

bool RespKv::set(const std::string& key, const std::string& value, int ttlSec,
                 std::string* err) {
  std::vector<std::string> args = {"SET", key, value};
  if (ttlSec > 0) {
    args.push_back("EX");
    args.push_back(std::to_string(ttlSec));
  }
  RespReply r;
  if (!client_.command(args, &r, err)) return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  return true;
}

bool RespKv::del(const std::string& key, std::string* err) {
  RespReply r;
  if (!client_.command({"DEL", key}, &r, err)) return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  return true;
}

bool RespKv::incr(const std::string& key, int64_t by, int64_t* out,
                  std::string* err) {
  RespReply r;
  if (!client_.command({"INCRBY", key, std::to_string(by)}, &r, err)) return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  if (out) *out = r.asInt();
  return true;
}

bool RespKv::expire(const std::string& key, int ttlSec, std::string* err) {
  RespReply r;
  if (!client_.command({"EXPIRE", key, std::to_string(ttlSec)}, &r, err))
    return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  return true;
}

bool RespKv::ttl(const std::string& key, int* out, std::string* err) {
  RespReply r;
  if (!client_.command({"TTL", key}, &r, err)) return false;
  if (!r.ok()) {
    if (err) *err = r.str;
    return false;
  }
  if (out) *out = (int)r.asInt();
  return true;
}

bool RespKv::ping(std::string* err) { return client_.ping(err); }

}  // namespace dw
