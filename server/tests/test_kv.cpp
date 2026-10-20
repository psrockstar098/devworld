#include "doctest.h"

#include "kv.h"
#include "util.h"

using namespace dw;

TEST_CASE("memory kv honours TTL with an injected clock") {
  int64_t now = 1000;
  MemoryKv kv([&] { return now; });
  std::string v;
  bool found = false;

  REQUIRE(kv.set("k", "v", 10, nullptr));
  REQUIRE(kv.get("k", &v, &found, nullptr));
  CHECK(found);
  CHECK(v == "v");

  now += 9'000;
  REQUIRE(kv.ttl("k", nullptr, nullptr));
  int ttl = 0;
  REQUIRE(kv.ttl("k", &ttl, nullptr));
  CHECK(ttl == 1);

  now += 1'000;
  REQUIRE(kv.get("k", &v, &found, nullptr));
  CHECK_FALSE(found);
  int missing = 0;
  REQUIRE(kv.ttl("k", &missing, nullptr));
  CHECK(missing == -2);
}

TEST_CASE("memory kv incr/del/expire") {
  MemoryKv kv;
  int64_t n = 0;
  REQUIRE(kv.incr("counter", 5, &n, nullptr));
  CHECK(n == 5);
  REQUIRE(kv.incr("counter", -2, &n, nullptr));
  CHECK(n == 3);

  std::string ns;
  bool found = false;
  REQUIRE(kv.get("counter", &ns, &found, nullptr));
  CHECK(found);
  CHECK(ns == "3");

  REQUIRE(kv.set("x", "y", 0, nullptr));
  int ttl = 0;
  REQUIRE(kv.ttl("x", &ttl, nullptr));
  CHECK(ttl == -1);
  REQUIRE(kv.expire("x", 30, nullptr));
  REQUIRE(kv.ttl("x", &ttl, nullptr));
  CHECK(ttl > 0);

  REQUIRE(kv.del("x", nullptr));
  REQUIRE(kv.get("x", &ns, &found, nullptr));
  CHECK_FALSE(found);
}

TEST_CASE("redis url parsing") {
  RedisUrl u;
  std::string err;
  REQUIRE(parseRedisUrl("redis://:secret@127.0.0.1:6666", &u, &err));
  CHECK(u.host == "127.0.0.1");
  CHECK(u.port == 6666);
  CHECK(u.password == "secret");

  REQUIRE(parseRedisUrl("redis://localhost", &u, &err));
  CHECK(u.host == "localhost");
  CHECK(u.port == 6379);
  CHECK(u.password.empty());
}

// These run only when the real stores are up; otherwise they are informational
// no-ops so the unit suite never depends on Docker/podman.
TEST_CASE("resp kv round-trips against a real redis when available") {
  RespKv kv;
  std::string err;
  if (!kv.connect("127.0.0.1", 6379, "devworld_dev_pw", 300, &err)) {
    MESSAGE("redis unavailable, skipping: " << err);
    return;
  }
  std::string key = "devworld:test:" + randomHex(6);
  REQUIRE(kv.set(key, "hello", 30, &err));
  std::string v;
  bool found = false;
  REQUIRE(kv.get(key, &v, &found, &err));
  CHECK(found);
  CHECK(v == "hello");
  int ttl = 0;
  REQUIRE(kv.ttl(key, &ttl, &err));
  CHECK(ttl > 0);
  REQUIRE(kv.del(key, &err));
}

TEST_CASE("resp kv round-trips against kvrocks when available") {
  RespKv kv;
  std::string err;
  if (!kv.connect("127.0.0.1", 6666, "devworld_dev_pw", 300, &err)) {
    MESSAGE("kvrocks unavailable, skipping: " << err);
    return;
  }
  std::string key = "devworld:test:" + randomHex(6);
  REQUIRE(kv.set(key, "durable", 0, &err));
  int64_t n = 0;
  REQUIRE(kv.incr(key + ":n", 7, &n, &err));
  CHECK(n == 7);
  REQUIRE(kv.del(key, &err));
  REQUIRE(kv.del(key + ":n", &err));
}
