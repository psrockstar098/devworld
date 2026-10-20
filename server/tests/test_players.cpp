#include "doctest.h"

#include "players.h"
#include "sessions.h"
#include "util.h"

using namespace dw;

TEST_CASE("players are created with a hashed password and a public projection") {
  MemoryKv kv;
  PlayerStore store(&kv);
  PlayerRecord rec;
  int status = 0;
  std::string err;
  REQUIRE(store.create("Alice_1", "hunter2hunter2", "Alice", &rec, &status, &err));
  CHECK(status == 0);
  CHECK(rec.id.size() >= 6);
  CHECK(rec.username == "alice_1");
  CHECK(rec.handle == "alice_1");
  CHECK(rec.passwordHash.rfind("pbkdf2_sha256$", 0) == 0);

  PlayerRecord got;
  bool found = false;
  REQUIRE(store.get(rec.id, &got, &found, &err));
  CHECK(found);
  CHECK(got.username == "alice_1");

  REQUIRE(store.getByUsername("ALICE_1", &got, &found, &err));
  CHECK(found);
  CHECK(got.id == rec.id);

  std::string pub = dumpJson(got.publicJson());
  CHECK(pub.find("passwordHash") == std::string::npos);
  CHECK(pub.find("username") != std::string::npos);
}

TEST_CASE("duplicate usernames and invalid credentials are rejected") {
  MemoryKv kv;
  PlayerStore store(&kv);
  PlayerRecord rec;
  int status = 0;
  std::string err;
  REQUIRE(store.create("bob", "password123", "", &rec, &status, &err));

  PlayerRecord dup;
  CHECK_FALSE(store.create("BOB", "password123", "", &dup, &status, &err));
  CHECK(status == 409);
  CHECK(err == "username already taken");

  CHECK_FALSE(store.create("x", "password123", "", &dup, &status, &err));
  CHECK(status == 400);
  CHECK_FALSE(store.create("carol", "short", "", &dup, &status, &err));
  CHECK(status == 400);
}

TEST_CASE("a username that is not a valid handle gets a safe handle") {
  MemoryKv kv;
  PlayerStore store(&kv);
  PlayerRecord rec;
  int status = 0;
  std::string err;
  REQUIRE(store.create("very_long_username_abcdef", "password123", "", &rec, &status, &err));
  CHECK(PlayerStore::validHandle(rec.handle));
  CHECK(rec.handle.rfind("player_", 0) == 0);
}

TEST_CASE("markCompleted is idempotent") {
  MemoryKv kv;
  PlayerStore store(&kv);
  PlayerRecord rec;
  int status = 0;
  std::string err;
  REQUIRE(store.create("dave", "password123", "", &rec, &status, &err));
  REQUIRE(store.markCompleted(rec.id, "m01", &err));
  REQUIRE(store.markCompleted(rec.id, "m01", &err));
  PlayerRecord got;
  bool found = false;
  REQUIRE(store.get(rec.id, &got, &found, &err));
  REQUIRE(found);
  CHECK(got.completedMissions.size() == 1);
}

TEST_CASE("sessions resolve, slide and destroy") {
  int64_t now = 1000;
  MemoryKv cache([&] { return now; });
  SessionStore sessions(&cache, 10);
  std::string err;
  IssuedSession issued;
  REQUIRE(sessions.create("p1", &issued, &err));
  CHECK(issued.expiresInSec == 10);
  CHECK(issued.token.size() >= 40);

  std::string pid;
  bool found = false;
  REQUIRE(sessions.resolve(issued.token, &pid, &found, &err));
  CHECK(found);
  CHECK(pid == "p1");

  now += 8000;
  REQUIRE(sessions.touch(issued.token, &err));
  now += 5000;
  REQUIRE(sessions.resolve(issued.token, &pid, &found, &err));
  CHECK(found);  // slid forward

  REQUIRE(sessions.destroy(issued.token, &err));
  REQUIRE(sessions.resolve(issued.token, &pid, &found, &err));
  CHECK_FALSE(found);
}

TEST_CASE("sessions expire without a touch") {
  int64_t now = 1000;
  MemoryKv cache([&] { return now; });
  SessionStore sessions(&cache, 5);
  std::string err;
  IssuedSession issued;
  REQUIRE(sessions.create("p1", &issued, &err));
  now += 6000;
  std::string pid;
  bool found = false;
  REQUIRE(sessions.resolve(issued.token, &pid, &found, &err));
  CHECK_FALSE(found);
}
