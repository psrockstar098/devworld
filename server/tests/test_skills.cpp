#include "doctest.h"

#include "api.h"
#include "json.h"
#include "kv.h"
#include "mastery.h"
#include "missions.h"
#include "players.h"
#include "sessions.h"
#include "skills.h"
#include "util.h"

using namespace dw;

namespace {

struct Fixture {
  MemoryKv kv;
  MemoryKv cache;
  MemoryHistory history;
  PlayerStore players{&kv};
  SessionStore sessions{&cache, 3600};
  MissionCatalog missions;
  ApiConfig cfg;

  Fixture() { cfg.adminKey = "test_admin"; }
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

std::string issueToken(Api& api, const std::string& username) {
  HttpResponse res = api.handle(req("POST", "/auth/register",
                                    "{\"username\":\"" + username +
                                        "\",\"password\":\"password123\"}"));
  REQUIRE(res.status == 201);
  return parseJson(res.body).s("token");
}

}  // namespace

TEST_CASE("evidence store round-trips and deduplicates") {
  MemoryKv kv;
  std::string err;
  Evidence e = makeEvidence("p1", "loops", EvidenceKind::AutomatedTest, 1.0,
                            1000, "m01", "");
  bool dup = false;
  std::vector<Evidence> updated;
  REQUIRE(recordEvidence(&kv, e, &dup, &updated, &err));
  CHECK_FALSE(dup);
  CHECK(updated.size() == 1);
  // The same observation again is a duplicate, not a second score.
  REQUIRE(recordEvidence(&kv, e, &dup, &updated, &err));
  CHECK(dup);
  CHECK(updated.size() == 1);
  // A different kind is a different observation.
  Evidence t = makeEvidence("p1", "loops", EvidenceKind::Transfer, 1.0, 2000,
                            "m09", "");
  REQUIRE(recordEvidence(&kv, t, &dup, &updated, &err));
  CHECK_FALSE(dup);
  CHECK(updated.size() == 2);
  // Reload from the store: both entries survive.
  std::vector<Evidence> loaded;
  REQUIRE(loadEvidence(&kv, "p1", &loaded, &err));
  CHECK(loaded.size() == 2);
  CHECK(loaded[0].skill == "loops");
  // A bad entry among good ones is skipped, not fatal.
  REQUIRE(kv.set(evidenceStoreKey("p1"),
                 "[{\"skill\":\"loops\",\"kind\":\"automated_test\",\"score\":1,"
                 "\"atMs\":1000},{\"skill\":\"\",\"kind\":\"nope\"}]",
                 0, &err));
  REQUIRE(loadEvidence(&kv, "p1", &loaded, &err));
  CHECK(loaded.size() == 1);
  // A corrupt envelope is an error, not a silent empty list.
  REQUIRE(kv.set(evidenceStoreKey("p1"), "not json", 0, &err));
  CHECK_FALSE(loadEvidence(&kv, "p1", &loaded, &err));
}

TEST_CASE("skills endpoints require auth and validate evidence") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  CHECK(api.handle(req("GET", "/skills/me")).status == 401);
  CHECK(api.handle(req("POST", "/skills/evidence", "{}")).status == 401);
  std::string token = issueToken(api, "skiller");
  CHECK(api.handle(req("POST", "/skills/evidence",
                         "{\"skill\":\"loops\",\"kind\":\"teleport\",\"score\":1}",
                         token))
            .status == 400);
  CHECK(api.handle(req("POST", "/skills/evidence",
                         "{\"skill\":\"loops\",\"kind\":\"automated_test\"}",
                         token))
            .status == 400);
  CHECK(api.handle(req("POST", "/skills/evidence",
                         "{\"skill\":\"loops\",\"kind\":\"automated_test\","
                         "\"score\":5}",
                         token))
            .status == 400);
  CHECK(api.handle(req("POST", "/skills/evidence",
                         "{\"skill\":\"\",\"kind\":\"automated_test\",\"score\":1}",
                         token))
            .status == 400);
}

TEST_CASE("skills endpoints record evidence and summarize mastery") {
  Fixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  std::string token = issueToken(api, "skiller");
  // Evidence timestamps use the test's own clock: the summary decays by
  // age (60-day half-life), so 1970 timestamps would score ~0.
  const int64_t t0 = nowMs();
  const std::string t0s = std::to_string(t0);
  // Tests alone: a good score, but never independent, never mastered.
  // That is the model's core distinction, pinned at the API level.
  HttpResponse r1 = api.handle(
      req("POST", "/skills/evidence",
          "{\"skill\":\"loops\",\"kind\":\"automated_test\",\"score\":1,"
          "\"source\":\"m01\",\"atMs\":" +
              t0s + "}",
          token));
  REQUIRE(r1.status == 201);
  JVal s1 = parseJson(r1.body);
  CHECK(s1.s("skill") == "loops");
  CHECK(s1.n("score", 0) > 0.99);
  CHECK_FALSE(s1.boolean("independent"));
  CHECK_FALSE(s1.boolean("mastered"));
  // The identical observation again is a duplicate (same explicit atMs:
  // the default would be nowMs(), which differs between the two posts).
  HttpResponse r2 = api.handle(
      req("POST", "/skills/evidence",
          "{\"skill\":\"loops\",\"kind\":\"automated_test\",\"score\":1,"
          "\"source\":\"m01\",\"atMs\":" +
              t0s + "}",
          token));
  REQUIRE(r2.status == 200);
  CHECK(parseJson(r2.body).boolean("duplicate"));
  // Transfer evidence on top of working code: independent and mastered.
  HttpResponse r3 = api.handle(
      req("POST", "/skills/evidence",
          "{\"skill\":\"loops\",\"kind\":\"transfer\",\"score\":1,"
          "\"source\":\"m09\",\"atMs\":" +
              t0s + "}",
          token));
  REQUIRE(r3.status == 201);
  JVal s3 = parseJson(r3.body);
  CHECK(s3.boolean("independent"));
  CHECK(s3.boolean("mastered"));
  // GET /skills/me reflects the same summary.
  HttpResponse me = api.handle(req("GET", "/skills/me", "", token));
  REQUIRE(me.status == 200);
  JVal mj = parseJson(me.body);
  const JVal& skills = mj.at("skills");
  REQUIRE(skills.arr.size() == 1);
  CHECK(skills.arr[0].s("skill") == "loops");
  CHECK(skills.arr[0].boolean("mastered"));
}
