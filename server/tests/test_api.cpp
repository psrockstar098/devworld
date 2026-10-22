#include "doctest.h"

#include <chrono>
#include <thread>

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

const char* kCurriculum = R"({
  "areas": [
    {"id": "fundamentals", "title": "Fundamentals", "summary": "The basics"}
  ],
  "topics": [
    {"id": "variables.expressions", "area": "fundamentals", "title": "Expressions",
     "summary": "Values and operators", "difficulty": 1, "prerequisites": [],
     "missions": ["m01"], "projects": []},
    {"id": "variables.assignment", "area": "fundamentals", "title": "Assignment",
     "summary": "Binding names", "difficulty": 2, "prerequisites": ["variables.expressions"],
     "missions": [], "projects": ["calculator"]}
  ]
})";

struct Fixture {
  MemoryKv kv;
  MemoryKv cache;
  MemoryHistory history;
  PlayerStore players{&kv};
  SessionStore sessions{&cache, 3600};
  MissionCatalog missions;
  Curriculum curriculum;
  ApiConfig cfg;

  Fixture() {
    std::string err;
    REQUIRE(missions.loadFromJson(parseJson(kContent), &err));
    curriculum.load(parseJson(kCurriculum));
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

  // A first full pass writes world history, rewards included.
  HttpResponse log = api.handle(req("GET", "/events", "", token));
  REQUIRE(log.status == 200);
  JVal logBody = parseJson(log.body);
  const JVal& events = logBody.at("events");
  REQUIRE(events.arr.size() == 2);
  CHECK(events.arr[0].s("type") == "mission.completed");
  CHECK(events.arr[0].s("title") == "builder completed First Boot");
  CHECK(events.arr[0].s("detail") == "+150 XP, +10 credits");
  CHECK(events.arr[1].s("type") == "player.joined");

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

TEST_CASE("world events: admin records, players read") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  std::string token = issueToken(api, "starter");

  // Recording needs the admin key; reading needs a session.
  CHECK(api.handle(req("POST", "/events",
                       "{\"type\":\"company.founded\",\"title\":\"Acme\"}"))
            .status == 401);
  HttpRequest wrong = req("POST", "/events",
                          "{\"type\":\"company.founded\",\"title\":\"Acme\"}");
  wrong.headers["x-admin-key"] = "wrong";
  CHECK(api.handle(wrong).status == 401);
  CHECK(api.handle(req("GET", "/events")).status == 401);

  HttpRequest ok = req(
      "POST", "/events",
      "{\"type\":\"company.founded\",\"title\":\"Acme\","
      "\"detail\":\"two founders and a laptop\",\"city\":\"startup_city\"}");
  ok.headers["x-admin-key"] = "test_admin";
  HttpResponse created = api.handle(ok);
  REQUIRE(created.status == 201);
  JVal cj = parseJson(created.body);
  CHECK(cj.s("type") == "company.founded");
  CHECK(cj.s("title") == "Acme");
  CHECK(cj.s("detail") == "two founders and a laptop");
  CHECK(cj.s("city") == "startup_city");
  CHECK(cj.s("id").size() > 5);
  CHECK(cj.at("createdAtMs").type == JVal::NUM);

  // Newest first. Registration itself wrote a player.joined event, so the
  // log holds both and the world writes its own history.
  HttpResponse listed = api.handle(req("GET", "/events", "", token));
  REQUIRE(listed.status == 200);
  JVal listedBody = parseJson(listed.body);
  const JVal& events = listedBody.at("events");
  REQUIRE(events.arr.size() == 2);
  CHECK(events.arr[0].s("type") == "company.founded");
  CHECK(events.arr[0].s("title") == "Acme");
  CHECK(events.arr[1].s("type") == "player.joined");
  CHECK(events.arr[1].s("title").find("joined the world") != std::string::npos);
}

TEST_CASE("world events: filters, limits and validation") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  std::string token = issueToken(api, "editor");

  auto record = [&](const std::string& body) {
    HttpRequest r = req("POST", "/events", body);
    r.headers["x-admin-key"] = "test_admin";
    return api.handle(r);
  };
  REQUIRE(record("{\"type\":\"company.founded\",\"title\":\"First\"}").status ==
          201);
  REQUIRE(record("{\"type\":\"product.launched\",\"title\":\"Second\"}")
              .status == 201);
  REQUIRE(record("{\"type\":\"company.founded\",\"title\":\"Third\"}").status ==
          201);

  HttpRequest byType = req("GET", "/events", "", token);
  byType.params["type"] = "company.founded";
  HttpResponse filtered = api.handle(byType);
  REQUIRE(filtered.status == 200);
  JVal filteredBody = parseJson(filtered.body);
  const JVal& fEvents = filteredBody.at("events");
  REQUIRE(fEvents.arr.size() == 2);
  CHECK(fEvents.arr[0].s("title") == "Third");
  CHECK(fEvents.arr[1].s("title") == "First");

  HttpRequest limited = req("GET", "/events", "", token);
  limited.params["limit"] = "2";
  HttpResponse recent = api.handle(limited);
  REQUIRE(recent.status == 200);
  JVal recentBody = parseJson(recent.body);
  const JVal& rEvents = recentBody.at("events");
  REQUIRE(rEvents.arr.size() == 2);
  CHECK(rEvents.arr[0].s("title") == "Third");
  CHECK(rEvents.arr[1].s("title") == "Second");

  // Limits must be positive numbers; types must be dotted identifiers.
  HttpRequest zero = req("GET", "/events", "", token);
  zero.params["limit"] = "0";
  CHECK(api.handle(zero).status == 400);
  HttpRequest nan = req("GET", "/events", "", token);
  nan.params["limit"] = "abc";
  CHECK(api.handle(nan).status == 400);
  HttpRequest badType = req("GET", "/events", "", token);
  badType.params["type"] = "bad type!";
  CHECK(api.handle(badType).status == 400);

  CHECK(record("{\"type\":\"bad type!\",\"title\":\"X\"}").status == 400);
  CHECK(record("{\"type\":\"company.founded\",\"title\":\"\"}").status == 400);
  CHECK(record("{\"type\":\"company.founded\",\"title\":\"" +
               std::string(201, 'x') + "\"}")
            .status == 400);
  CHECK(record("{\"type\":\"company.founded\",\"title\":\"X\",\"detail\":\"" +
               std::string(2001, 'd') + "\"}")
            .status == 400);
  CHECK(record("{\"type\":\"company.founded\",\"title\":\"X\",\"actorId\":\"" +
               std::string(65, 'a') + "\"}")
            .status == 400);
}

TEST_CASE("world events cannot be recorded without an admin key") {
  Fixture f;
  f.cfg.adminKey.clear();
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  HttpRequest r = req("POST", "/events",
                      "{\"type\":\"company.founded\",\"title\":\"X\"}");
  r.headers["x-admin-key"] = "anything";
  CHECK(api.handle(r).status == 404);
}

TEST_CASE("login with a bare player id is rejected") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  HttpResponse reg = api.handle(req(
      "POST", "/auth/register",
      "{\"username\":\"alice\",\"password\":\"password123\"}"));
  REQUIRE(reg.status == 201);
  std::string pid = parseJson(reg.body).at("player").s("id");
  // The old passwordless path must never issue a session, even for a real id.
  HttpResponse res = api.handle(req(
      "POST", "/auth/login", "{\"playerId\":\"" + pid + "\"}"));
  CHECK(res.status == 400);
  // Password login still works.
  HttpResponse ok = api.handle(req(
      "POST", "/auth/login",
      "{\"username\":\"alice\",\"password\":\"password123\"}"));
  CHECK(ok.status == 200);
}

TEST_CASE("auth endpoints are rate limited per ip and per account") {
  Fixture f;
  f.cfg.authRateLimitWindowSec = 60;
  f.cfg.authRateLimitMaxPerIp = 3;
  f.cfg.authRateLimitMaxPerAccount = 2;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  auto authed = [&](const std::string& path, const std::string& body,
                    const std::string& peer) {
    HttpRequest r = req("POST", path, body);
    r.peer = peer;
    return api.handle(r);
  };

  // Per-IP: three registrations from one IP pass, the fourth is limited.
  for (int i = 0; i < 3; ++i) {
    HttpResponse res = authed("/auth/register",
        "{\"username\":\"user" + std::to_string(i) + "\",\"password\":\"password123\"}",
        "1.2.3.4:1000");
    CHECK(res.status == 201);
  }
  HttpResponse limited = authed("/auth/register",
      "{\"username\":\"user9\",\"password\":\"password123\"}", "1.2.3.4:1000");
  CHECK(limited.status == 429);
  CHECK(limited.headers.count("retry-after") == 1);

  // A different IP is unaffected.
  HttpResponse other = authed("/auth/register",
      "{\"username\":\"u10\",\"password\":\"password123\"}", "5.6.7.8:2000");
  CHECK(other.status == 201);

  // Per-account: the register counts once, so one wrong login passes and the
  // next is limited, even from a fresh IP.
  HttpResponse reg = authed("/auth/register",
      "{\"username\":\"victim\",\"password\":\"password123\"}", "5.6.7.9:2000");
  REQUIRE(reg.status == 201);
  HttpResponse first = authed("/auth/login",
      "{\"username\":\"victim\",\"password\":\"wrong\"}", "9.9.9.9:3000");
  CHECK(first.status == 401);
  HttpResponse third = authed("/auth/login",
      "{\"username\":\"victim\",\"password\":\"wrong\"}", "9.9.9.10:3000");
  CHECK(third.status == 429);
}

TEST_CASE("auth rate limit window resets") {
  Fixture f;
  f.cfg.authRateLimitWindowSec = 1;
  f.cfg.authRateLimitMaxPerIp = 1;
  f.cfg.authRateLimitMaxPerAccount = 10;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  auto reg = [&](const std::string& user) {
    HttpRequest r = req("POST", "/auth/register",
        "{\"username\":\"" + user + "\",\"password\":\"password123\"}");
    r.peer = "7.7.7.7:1000";
    return api.handle(r);
  };

  CHECK(reg("win0").status == 201);
  CHECK(reg("win1").status == 429);
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  CHECK(reg("win2").status == 201);
}

TEST_CASE("market: list, buy, inventory, restock") {
  Fixture f;
  MarketCatalog catalog;
  std::string err;
  REQUIRE(catalog.loadFromJson(parseJson(R"({
    "regions": [
      {"id": "starter_town", "name": "Starter Town", "livingCost": 0, "marketBias": 0},
      {"id": "startup_city", "name": "Startup City", "livingCost": 25, "marketBias": 20}
    ],
    "goods": [
      {"id": "cable-kit", "name": "Cable Kit", "category": "component", "basePrice": 8},
      {"id": "gpu-rtx", "name": "RTX GPU", "category": "component", "basePrice": 250},
      {"id": "old-phone", "name": "Old Phone", "category": "equipment", "basePrice": 30, "status": "discontinued", "replacedBy": "new-phone"},
      {"id": "new-phone", "name": "New Phone", "category": "equipment", "basePrice": 60}
    ]
  })"), &err));
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg, &catalog);

  std::string token = issueToken(api, "buyer");
  HttpResponse me = api.handle(req("GET", "/me", "", token));
  REQUIRE(me.status == 200);
  std::string pid = parseJson(me.body).s("id");

  // Fund the buyer with 100 credits through the admin ledger.
  HttpRequest fund = req(
      "POST", "/admin/ledger",
      "{\"entries\":["
      "{\"ref\":\"fund-1\",\"playerId\":\"" + pid + "\",\"account\":\"system\",\"amount\":-100},"
      "{\"ref\":\"fund-1\",\"playerId\":\"" + pid + "\",\"account\":\"wallet\",\"amount\":100}]}");
  fund.headers["x-admin-key"] = "test_admin";
  REQUIRE(api.handle(fund).status == 200);

  auto marketPrice = [&](const std::string& goodId) {
    HttpRequest r = req("GET", "/market", "", "");
    r.params["region"] = "startup_city";
    HttpResponse res = api.handle(r);
    REQUIRE(res.status == 200);
    JVal body = parseJson(res.body);
    for (const auto& g : body.at("goods").arr) {
      if (g.s("id") == goodId) return (int64_t)g.n("price", -1);
    }
    return (int64_t)-1;
  };
  auto buy = [&](const std::string& body) {
    return api.handle(req("POST", "/market/buy", body, token));
  };

  // Startup City bias is +20%: cable-kit lists at 10, not its base 8.
  CHECK(marketPrice("cable-kit") == 10);

  HttpResponse b1 = buy("{\"goodId\":\"cable-kit\",\"region\":\"startup_city\"}");
  REQUIRE(b1.status == 200);
  CHECK(parseJson(b1.body).n("total", -1) == 10);
  CHECK(parseJson(b1.body).n("balance", -1) == 90);
  // Demand 1 lifts the price only to 10.08, still 10 rounded.
  HttpResponse b2 = buy("{\"goodId\":\"cable-kit\",\"region\":\"startup_city\"}");
  REQUIRE(b2.status == 200);
  CHECK(parseJson(b2.body).n("total", -1) == 10);
  CHECK(parseJson(b2.body).n("balance", -1) == 80);
  // Demand 2 lifts it to 11.
  HttpResponse b3 = buy("{\"goodId\":\"cable-kit\",\"region\":\"startup_city\"}");
  REQUIRE(b3.status == 200);
  CHECK(parseJson(b3.body).n("total", -1) == 11);
  CHECK(parseJson(b3.body).n("balance", -1) == 69);
  CHECK(marketPrice("cable-kit") == 11);

  HttpResponse inv = api.handle(req("GET", "/market/inventory", "", token));
  REQUIRE(inv.status == 200);
  JVal items = parseJson(inv.body).at("items");
  REQUIRE(items.arr.size() == 1);
  CHECK(items.arr[0].s("goodId") == "cable-kit");
  CHECK(items.arr[0].n("qty", -1) == 3);

  // A 300-credit GPU is out of reach at balance 69.
  CHECK(buy("{\"goodId\":\"gpu-rtx\",\"region\":\"startup_city\"}").status == 400);
  // Discontinued goods cannot be bought, and bad regions/goods are rejected.
  CHECK(buy("{\"goodId\":\"old-phone\",\"region\":\"startup_city\"}").status == 400);
  CHECK(buy("{\"goodId\":\"cable-kit\",\"region\":\"nowhere\"}").status == 400);
  CHECK(buy("{\"goodId\":\"nope\",\"region\":\"startup_city\"}").status == 404);

  // Restock is admin-only and drops the price back.
  HttpRequest restock = req("POST", "/market/restock",
                            "{\"goodId\":\"cable-kit\",\"supply\":3}");
  CHECK(api.handle(restock).status == 401);
  restock.headers["x-admin-key"] = "test_admin";
  HttpResponse restocked = api.handle(restock);
  REQUIRE(restocked.status == 200);
  CHECK(parseJson(restocked.body).n("demand", -1) == 0);
  CHECK(marketPrice("cable-kit") == 10);
}

TEST_CASE("skills trees serve the area/topic structure") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg, nullptr, &f.curriculum);
  HttpResponse res = api.handle(req("GET", "/skills/trees"));
  REQUIRE(res.status == 200);
  JVal body = parseJson(res.body);
  auto areas = body.at("areas").arr;
  REQUIRE(areas.size() == 1);
  CHECK(areas[0].s("id") == "fundamentals");
  auto topics = areas[0].at("topics").arr;
  REQUIRE(topics.size() == 2);
  CHECK(topics[0].s("id") == "variables.expressions");
  CHECK(topics[0].i("difficulty", -1) == 1);
  CHECK(topics[0].at("missions").arr.size() == 1);
  CHECK(topics[1].s("id") == "variables.assignment");
  CHECK(topics[1].at("prerequisites").arr.size() == 1);
  CHECK(topics[1].at("prerequisites").arr[0].str == "variables.expressions");
  CHECK(topics[1].at("projects").arr.size() == 1);
}

TEST_CASE("skills trees without a curriculum answers 503") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  CHECK(api.handle(req("GET", "/skills/trees")).status == 503);
}
