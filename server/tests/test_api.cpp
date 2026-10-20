#include "doctest.h"

#include "api.h"
#include "clickhouse.h"
#include "json.h"
#include "kv.h"
#include "missions.h"
#include "players.h"
#include "sessions.h"
#include "util.h"

using namespace dw;

namespace {

const char* kContent = R"({
  "missions": [
    {
      "id": "m01", "title": "First Boot", "language": "python", "level": 1,
      "requires": [], "xpReward": 150, "creditReward": 10, "gate": false,
      "tests": [
        {"name": "greets", "stdin": "", "expected": "Hello", "hidden": false},
        {"name": "edge", "stdin": "x", "expected": "y", "hidden": true}
      ]
    }
  ]
})";

struct Fixture {
  MemoryKv kv;
  MemoryKv cache;
  MemoryHistory history;
  PlayerStore players{&kv};
  SessionStore sessions{&cache, 3600};
  MissionCatalog missions;
  ApiConfig cfg;

  Fixture() {
    std::string err;
    REQUIRE(missions.loadFromJson(parseJson(kContent), &err));
    cfg.adminKey = "test_admin";
    cfg.balanceCacheTtlSec = 0;
  }
};

HttpRequest req(const std::string& method, const std::string& path,
                const std::string& body = "", const std::string& token = "") {
  HttpRequest r;
  r.method = method;
  r.path = path;
  r.target = path;
  r.version = "HTTP/1.1";
  r.body = body;
  if (!token.empty()) r.headers["authorization"] = "Bearer " + token;
  return r;
}

std::string issueToken(Api& api, const std::string& username,
                       const std::string& password = "password123") {
  HttpResponse res = api.handle(req("POST", "/auth/register",
                                    "{\"username\":\"" + username +
                                        "\",\"password\":\"" + password + "\"}"));
  REQUIRE(res.status == 201);
  JVal j = parseJson(res.body);
  return j.s("token");
}

}  // namespace

TEST_CASE("register, login and me") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  HttpResponse reg = api.handle(req(
      "POST", "/auth/register",
      "{\"username\":\"alice\",\"password\":\"password123\",\"displayName\":\"Alice\"}"));
  REQUIRE(reg.status == 201);
  JVal rj = parseJson(reg.body);
  CHECK(rj.s("token").size() > 20);
  CHECK(rj.at("player").s("username") == "alice");
  CHECK(rj.at("player").s("passwordHash").empty());

  // Duplicate username.
  HttpResponse dup = api.handle(req(
      "POST", "/auth/register",
      "{\"username\":\"alice\",\"password\":\"password123\"}"));
  CHECK(dup.status == 409);

  // Wrong password.
  HttpResponse bad = api.handle(req(
      "POST", "/auth/login", "{\"username\":\"alice\",\"password\":\"nope\"}"));
  CHECK(bad.status == 401);

  // Correct password.
  HttpResponse login = api.handle(req(
      "POST", "/auth/login", "{\"username\":\"alice\",\"password\":\"password123\"}"));
  REQUIRE(login.status == 200);
  std::string token = parseJson(login.body).s("token");
  CHECK(token.size() > 20);

  // Unauthenticated.
  CHECK(api.handle(req("GET", "/me")).status == 401);

  HttpResponse me = api.handle(req("GET", "/me", "", token));
  REQUIRE(me.status == 200);
  JVal mj = parseJson(me.body);
  CHECK(mj.s("username") == "alice");
  CHECK(mj.at("progression").i("level") == 1);
  CHECK(mj.i("balance") == 0);

  // Logout invalidates the token.
  CHECK(api.handle(req("POST", "/auth/logout", "{}", token)).status == 200);
  CHECK(api.handle(req("GET", "/me", "", token)).status == 401);
}

TEST_CASE("validation and unknown routes") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  CHECK(api.handle(req("GET", "/nope")).status == 404);
  CHECK(api.handle(req("POST", "/auth/register", "[]")).status == 400);
  CHECK(api.handle(req("POST", "/auth/register", "not json")).status == 400);
  CHECK(api.handle(req("POST", "/auth/register",
                       "{\"username\":\"x\",\"password\":\"password123\"}"))
            .status == 400);

  HttpResponse opts = api.handle(req("OPTIONS", "/auth/login"));
  CHECK(opts.status == 204);
  CHECK(opts.headers["access-control-allow-origin"] == "*");
}

TEST_CASE("mission submission grades, rewards and updates progression") {
  Fixture f;

  // A real HTTP worker stub, served by the same stack the API uses.
  HttpServer worker(
      "127.0.0.1", 0,
      [](const HttpRequest&) {
        return HttpResponse::json(
            "{\"passed\":2,\"total\":2,\"durationMs\":3,\"results\":["
            "{\"name\":\"greets\",\"passed\":true,\"hidden\":false,\"actual\":\"Hello\"},"
            "{\"name\":\"edge\",\"passed\":true,\"hidden\":true}]}");
      });
  std::string err;
  REQUIRE(worker.start(&err));
  AssessClient assess("http://127.0.0.1:" + std::to_string(worker.port()));

  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          &assess, f.cfg);
  std::string token = issueToken(api, "builder");
  std::string pid = parseJson(api.handle(req("GET", "/me", "", token)).body).s("id");

  HttpResponse cat = api.handle(req("GET", "/missions"));
  REQUIRE(cat.status == 200);
  CHECK(parseJson(cat.body).at("missions").arr.size() == 1);

  HttpResponse submit = api.handle(req(
      "POST", "/missions/m01/submit",
      "{\"files\":[{\"path\":\"solution.py\",\"content\":\"print('Hello')\"}]}",
      token));
  REQUIRE(submit.status == 200);
  JVal sj = parseJson(submit.body);
  CHECK(sj.boolean("passed"));
  CHECK(sj.boolean("rewarded"));
  CHECK(sj.i("publicPassed") == 1);
  CHECK(sj.i("hiddenPassed") == 1);
  CHECK(sj.at("progression").i("level") == 2);
  CHECK(sj.i("balance") == 10);

  // The hidden test never leaks its expected output.
  const JVal& results = sj.at("results");
  REQUIRE(results.arr.size() == 2);
  CHECK(results.arr[1].boolean("hidden"));

  // Completion, XP and ledger are durable.
  HttpResponse me = api.handle(req("GET", "/me", "", token));
  JVal mj = parseJson(me.body);
  CHECK(mj.i("balance") == 10);
  CHECK(mj.at("progression").i("level") == 2);
  CHECK(mj.at("completedMissions").arr.size() == 1);

  // Replaying the same mission is rejected.
  HttpResponse again = api.handle(req(
      "POST", "/missions/m01/submit",
      "{\"files\":[{\"path\":\"solution.py\",\"content\":\"print('Hello')\"}]}",
      token));
  CHECK(again.status == 409);

  // Admin ledger: credit 5 more, then the cached balance must reflect it.
  HttpRequest admin = req(
      "POST", "/admin/ledger",
      "{\"entries\":["
      "{\"ref\":\"admin-1\",\"playerId\":\"" + pid + "\",\"account\":\"system\",\"amount\":-5},"
      "{\"ref\":\"admin-1\",\"playerId\":\"" + pid + "\",\"account\":\"wallet\",\"amount\":5}]}");
  admin.headers["x-admin-key"] = "test_admin";
  HttpResponse ledger = api.handle(admin);
  REQUIRE(ledger.status == 200);

  HttpResponse bal = api.handle(req("GET", "/ledger/balance", "", token));
  REQUIRE(bal.status == 200);
  CHECK(parseJson(bal.body).i("balance") == 15);

  // An unbalanced admin transaction is rejected.
  HttpRequest bad = req(
      "POST", "/admin/ledger",
      "{\"entries\":["
      "{\"ref\":\"bad\",\"playerId\":\"" + pid + "\",\"account\":\"system\",\"amount\":-5},"
      "{\"ref\":\"bad\",\"playerId\":\"" + pid + "\",\"account\":\"wallet\",\"amount\":4}]}");
  bad.headers["x-admin-key"] = "test_admin";
  CHECK(api.handle(bad).status == 400);

  worker.stop();
}

TEST_CASE("submission requires a configured grading backend") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  std::string token = issueToken(api, "nograding");
  HttpResponse res = api.handle(req(
      "POST", "/missions/m01/submit",
      "{\"files\":[{\"path\":\"a.py\",\"content\":\"x\"}]}", token));
  CHECK(res.status == 503);
}

TEST_CASE("health reports the stores") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  HttpResponse res = api.handle(req("GET", "/health"));
  CHECK(res.status == 200);
  JVal j = parseJson(res.body);
  CHECK(j.boolean("ok"));
  CHECK(j.at("stores").boolean("clickhouse"));
  CHECK(j.at("stores").boolean("kvrocks"));
  CHECK(j.at("stores").boolean("redis"));
}
