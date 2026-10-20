#pragma once
// kv.h — the key-value seam. `RespKv` talks to a real Redis-compatible server
// (Redis for the cache, Kvrocks for durable current state); `MemoryKv` is the
// deterministic double used by unit tests.
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "resp.h"

namespace dw {

struct RedisUrl {
  std::string host = "127.0.0.1";
  int port = 6379;
  std::string password;
};

// redis://[:password@]host[:port][/db]
bool parseRedisUrl(const std::string& url, RedisUrl* out, std::string* err);

class KvStore {
 public:
  virtual ~KvStore() = default;
  virtual bool get(const std::string& key, std::string* value, bool* found,
                   std::string* err) = 0;
  virtual bool set(const std::string& key, const std::string& value, int ttlSec,
                   std::string* err) = 0;
  virtual bool del(const std::string& key, std::string* err) = 0;
  virtual bool incr(const std::string& key, int64_t by, int64_t* out,
                    std::string* err) = 0;
  virtual bool expire(const std::string& key, int ttlSec, std::string* err) = 0;
  virtual bool ttl(const std::string& key, int* out, std::string* err) = 0;
  virtual bool ping(std::string* err) = 0;
  virtual void close() {}
};

class MemoryKv : public KvStore {
 public:
  explicit MemoryKv(std::function<int64_t()> now = nullptr);

  bool get(const std::string& key, std::string* value, bool* found,
           std::string* err) override;
  bool set(const std::string& key, const std::string& value, int ttlSec,
           std::string* err) override;
  bool del(const std::string& key, std::string* err) override;
  bool incr(const std::string& key, int64_t by, int64_t* out,
            std::string* err) override;
  bool expire(const std::string& key, int ttlSec, std::string* err) override;
  bool ttl(const std::string& key, int* out, std::string* err) override;
  bool ping(std::string* err) override;

  size_t size() const;

 private:
  struct Entry {
    std::string value;
    int64_t expiresAt = -1;  // ms, -1 = no expiry
  };
  Entry* live(const std::string& key);
  mutable std::mutex mu_;
  std::map<std::string, Entry> map_;
  std::function<int64_t()> now_;
};

class RespKv : public KvStore {
 public:
  RespKv() = default;
  bool connect(const std::string& host, int port, const std::string& password,
               int timeoutMs, std::string* err);
  bool connectUrl(const std::string& url, int timeoutMs, std::string* err);
  void close() override;

  bool get(const std::string& key, std::string* value, bool* found,
           std::string* err) override;
  bool set(const std::string& key, const std::string& value, int ttlSec,
           std::string* err) override;
  bool del(const std::string& key, std::string* err) override;
  bool incr(const std::string& key, int64_t by, int64_t* out,
            std::string* err) override;
  bool expire(const std::string& key, int ttlSec, std::string* err) override;
  bool ttl(const std::string& key, int* out, std::string* err) override;
  bool ping(std::string* err) override;

  // Escape hatch for operations the seam does not model (pub/sub, scripts).
  bool raw(const std::vector<std::string>& args, RespReply* out,
           std::string* err) {
    return client_.command(args, out, err);
  }

 private:
  RespClient client_;
};

}  // namespace dw
