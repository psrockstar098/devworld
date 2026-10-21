#include "doctest.h"

#include <memory>
#include <mutex>
#include <string>

#include "api.h"
#include "clickhouse.h"
#include "http.h"
#include "json.h"
#include "kv.h"
#include "missions.h"
#include "oauth.h"
#include "players.h"
#include "sessions.h"
#include "util.h"

using namespace dw;

namespace {

const char* kContent = R"({"missions": []})";

HttpResponse jsonRes(const std::string& body, int status = 200) {
  return HttpResponse::json(body, status);
}

struct MockState {
  std::mutex mu;
  std::string lastTokenForm;
  int tokenCalls = 0;
};

HttpRequest req(const std::string& method, const std::string& path,
                const std::string& token = "") {
  HttpRequest r;
  r.method = method;
  r.path = path;
  r.target = path;
  r.version = "HTTP/1.1";
  if (!token.empty()) r.headers["authorization"] = "Bearer " + token;
  return r;
}

OAuthProvider googleProvider(const std::string& base) {
  OAuthProvider p;
  p.id = "google";
  p.name = "Google";
  p.kind = OAuthProviderKind::Google;
  p.clientId = "google-client";
  p.clientSecret = "google-secret";
  p.authorizeUrl = base + "/google/authorize";
  p.tokenUrl = base + "/google/token";
  p.userInfoUrl = base + "/google/user";
  p.scope = "openid email profile";
  p.pkce = true;
  return p;
}

OAuthProvider facebookProvider(const std::string& base) {
  OAuthProvider p;
  p.id = "facebook";
  p.name = "Facebook";
  p.kind = OAuthProviderKind::Facebook;
  p.clientId = "facebook-client";
  p.clientSecret = "facebook-secret";
  p.authorizeUrl = base + "/facebook/authorize";
  p.tokenUrl = base + "/facebook/token";
  p.userInfoUrl = base + "/facebook/user";
  p.scope = "email,public_profile";
  p.pkce = false;
  return p;
}

OAuthProvider githubProvider(const std::string& base) {
  OAuthProvider p;
  p.id = "github";
  p.name = "GitHub";
  p.kind = OAuthProviderKind::GitHub;
  p.clientId = "github-client";
  p.clientSecret = "github-secret";
  p.authorizeUrl = base + "/github/authorize";
  p.tokenUrl = base + "/github/token";
  p.userInfoUrl = base + "/github/user";
  p.emailsUrl = base + "/github/emails";
  p.scope = "read:user user:email";
  p.pkce = false;
  return p;
}

struct OAuthFixture {
  MemoryKv kv;
  MemoryKv cache;
  MemoryHistory history;
  PlayerStore players{&kv};
  SessionStore sessions{&cache, 3600};
  MissionCatalog missions;
  ApiConfig cfg;
  std::shared_ptr<MockState> mockState = std::make_shared<MockState>();
  HttpServer mock;
  std::string mockBase;

  OAuthFixture()
      : mock("127.0.0.1", 0,
             [this](const HttpRequest& r) { return handleMock(r); }) {
    std::string err;
    REQUIRE(missions.loadFromJson(parseJson(kContent), &err));
    REQUIRE(mock.start(&err));
    mockBase = "http://127.0.0.1:" + std::to_string(mock.port());

    cfg.adminKey = "test_admin";
    cfg.balanceCacheTtlSec = 0;
    cfg.oauth.baseUrl = "http://127.0.0.1:8091";
    cfg.oauth.allowedOrigins =
        "http://localhost:8000,http://127.0.0.1:8000";
    cfg.oauth.providers = {googleProvider(mockBase), githubProvider(mockBase),
                           facebookProvider(mockBase)};
  }

  HttpResponse handleMock(const HttpRequest& r) {
    if (r.path == "/google/token" || r.path == "/github/token" ||
        r.path == "/facebook/token") {
      std::lock_guard<std::mutex> lock(mockState->mu);
      ++mockState->tokenCalls;
      mockState->lastTokenForm = r.body;
      if (r.body.find("code=code-1") == std::string::npos)
        return jsonRes("{\"error\":\"bad_code\"}", 400);
      return jsonRes("{\"access_token\":\"access-token\",\"token_type\":"
                     "\"Bearer\"}");
    }
    if (r.path == "/google/user") {
      return jsonRes(
          "{\"sub\":\"google-user-1\",\"email\":\"new@example.com\","
          "\"email_verified\":true,\"name\":\"New Person\","
          "\"picture\":\"https://example.com/p.png\"}");
    }
    if (r.path == "/github/user") {
      return jsonRes(
          "{\"id\":4242,\"login\":\"octocat\",\"name\":\"Octo Cat\","
          "\"email\":null,\"avatar_url\":\"https://example.com/o.png\"}");
    }
    if (r.path == "/facebook/user") {
      // Facebook does not assert email_verified; the email must not become a
      // linking key.
      return jsonRes(
          "{\"id\":\"fb-user-1\",\"name\":\"FB Person\","
          "\"email\":\"new@example.com\"}");
    }
    if (r.path == "/github/emails") {
      return jsonRes(
          "[{\"email\":\"new@example.com\",\"primary\":true,"
          "\"verified\":true}]");
    }
    return HttpResponse::error(404, "no such mock route");
  }
};

std::string cookieValue(const HttpResponse& res) {
  auto it = res.headers.find("set-cookie");
  if (it == res.headers.end()) return "";
  std::string raw = it->second;
  size_t eq = raw.find('=');
  size_t semi = raw.find(';');
  if (eq == std::string::npos) return "";
  return raw.substr(eq + 1, semi == std::string::npos ? std::string::npos
                                                       : semi - eq - 1);
}

std::string queryParam(const std::string& url, const std::string& name) {
  size_t q = url.find('?');
  if (q == std::string::npos) return "";
  for (const auto& pair : split(url.substr(q + 1), '&')) {
    size_t eq = pair.find('=');
    if (eq == std::string::npos) continue;
    if (urlDecode(pair.substr(0, eq)) == name)
      return urlDecode(pair.substr(eq + 1));
  }
  return "";
}

struct Flow {
  std::string state;
  std::string cookie;
  std::string authorizeUrl;
};

Flow startFlow(Api& api, const std::string& provider) {
  HttpRequest r = req("GET", "/auth/oauth/" + provider + "/start");
  r.params["returnTo"] = "http://localhost:8000/";
  r.params["format"] = "json";
  HttpResponse res = api.handle(r);
  REQUIRE(res.status == 302);
  Flow flow;
  auto it = res.headers.find("location");
  REQUIRE(it != res.headers.end());
  flow.authorizeUrl = it->second;
  flow.state = queryParam(flow.authorizeUrl, "state");
  flow.cookie = cookieValue(res);
  REQUIRE(flow.state.size() > 10);
  REQUIRE(flow.cookie == flow.state);
  return flow;
}

HttpResponse callback(Api& api, const std::string& provider, const Flow& flow,
                      const std::string& cookie) {
  HttpRequest r = req("GET", "/auth/oauth/" + provider + "/callback");
  r.params["code"] = "code-1";
  r.params["state"] = flow.state;
  r.params["format"] = "json";
  if (!cookie.empty()) r.headers["cookie"] = "dw_oauth_state=" + cookie;
  return api.handle(r);
}

}  // namespace

TEST_CASE("oauth provider list and start guards") {
  OAuthFixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  HttpResponse list = api.handle(req("GET", "/auth/providers"));
  REQUIRE(list.status == 200);
  JVal lj = parseJson(list.body);
  CHECK(lj.boolean("configured"));
  REQUIRE(lj.at("providers").arr.size() == 3);
  CHECK(lj.at("providers").arr[0].s("id") == "google");

  // An origin outside the allow-list must not become a redirect target.
  HttpRequest bad = req("GET", "/auth/oauth/google/start");
  bad.params["returnTo"] = "http://evil.example/steal";
  bad.params["format"] = "json";
  CHECK(api.handle(bad).status == 400);

  Flow flow = startFlow(api, "google");
  CHECK(flow.authorizeUrl.rfind(f.mockBase + "/google/authorize?", 0) == 0);
  CHECK(queryParam(flow.authorizeUrl, "code_challenge").size() > 20);
  CHECK(queryParam(flow.authorizeUrl, "code_challenge_method") == "S256");
  CHECK(queryParam(flow.authorizeUrl, "client_id") == "google-client");
}

TEST_CASE("oauth callback creates a player and issues a session") {
  OAuthFixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  Flow flow = startFlow(api, "google");

  // The state cookie is part of CSRF protection.
  CHECK(callback(api, "google", flow, "").status == 400);
  CHECK(callback(api, "google", flow, "wrong-state").status == 400);

  HttpResponse cb = callback(api, "google", flow, flow.cookie);
  REQUIRE(cb.status == 200);
  JVal cj = parseJson(cb.body);
  CHECK(cj.s("provider") == "google");
  CHECK(cj.boolean("created"));
  std::string token = cj.s("token");
  std::string playerId = cj.at("player").s("id");
  REQUIRE(token.size() > 20);
  CHECK(cj.at("player").s("email") == "new@example.com");
  CHECK(cj.at("player").at("providers").arr[0].str == "google");

  HttpResponse me = api.handle(req("GET", "/me", token));
  REQUIRE(me.status == 200);
  JVal mj = parseJson(me.body);
  CHECK(mj.s("email") == "new@example.com");
  CHECK(mj.boolean("emailVerified"));
  CHECK(mj.at("providers").arr.size() == 1);

  // The same provider subject signs into the same account.
  Flow again = startFlow(api, "google");
  HttpResponse cb2 = callback(api, "google", again, again.cookie);
  REQUIRE(cb2.status == 200);
  JVal cj2 = parseJson(cb2.body);
  CHECK(cj2.at("player").s("id") == playerId);
  CHECK_FALSE(cj2.boolean("created"));

  // State is single use.
  CHECK(callback(api, "google", flow, flow.cookie).status == 400);

  // PKCE verifier was sent to the token endpoint.
  std::lock_guard<std::mutex> lock(f.mockState->mu);
  CHECK(f.mockState->tokenCalls >= 2);
  CHECK(f.mockState->lastTokenForm.find("code_verifier=") != std::string::npos);
}

TEST_CASE("callback keeps the start format when the redirect omits it") {
  OAuthFixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);
  Flow flow = startFlow(api, "google");

  // Providers only echo ?code and ?state, not the start query. The state record
  // remembers that this flow asked for JSON.
  HttpRequest r = req("GET", "/auth/oauth/google/callback");
  r.params["code"] = "code-1";
  r.params["state"] = flow.state;
  r.headers["cookie"] = "dw_oauth_state=" + flow.cookie;
  HttpResponse cb = api.handle(r);
  REQUIRE(cb.status == 200);
  CHECK(cb.headers.at("content-type").find("application/json") !=
        std::string::npos);
  CHECK(parseJson(cb.body).s("token").size() > 20);
}

TEST_CASE("an unverified email is not a linking key") {
  OAuthFixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  // Facebook hands back an email but never asserts it is verified. That must
  // not let a later provider with the same verified email inherit the account.
  Flow facebook = startFlow(api, "facebook");
  HttpResponse first = callback(api, "facebook", facebook, facebook.cookie);
  REQUIRE(first.status == 200);
  JVal fj = parseJson(first.body);
  CHECK(fj.boolean("created"));
  CHECK(fj.at("player").s("email") == "new@example.com");
  CHECK_FALSE(fj.at("player").boolean("emailVerified"));
  std::string facebookPlayer = fj.at("player").s("id");

  Flow google = startFlow(api, "google");
  HttpResponse second = callback(api, "google", google, google.cookie);
  REQUIRE(second.status == 200);
  JVal gj = parseJson(second.body);
  CHECK(gj.boolean("created"));
  CHECK(gj.at("player").s("id") != facebookPlayer);
  CHECK(gj.at("player").boolean("emailVerified"));
}

TEST_CASE("verified email links a second provider to the same player") {
  OAuthFixture f;
  Api api(&f.history, &f.kv, &f.cache, &f.players, &f.sessions, &f.missions,
          nullptr, f.cfg);

  Flow google = startFlow(api, "google");
  HttpResponse first = callback(api, "google", google, google.cookie);
  REQUIRE(first.status == 200);
  std::string playerId = parseJson(first.body).at("player").s("id");

  Flow github = startFlow(api, "github");
  HttpResponse second = callback(api, "github", github, github.cookie);
  REQUIRE(second.status == 200);
  JVal sj = parseJson(second.body);
  CHECK(sj.at("player").s("id") == playerId);
  CHECK_FALSE(sj.boolean("created"));
  JVal player = sj.at("player");
  REQUIRE(player.at("providers").arr.size() == 2);
  bool hasGoogle = false, hasGithub = false;
  for (const auto& p : player.at("providers").arr) {
    if (p.str == "google") hasGoogle = true;
    if (p.str == "github") hasGithub = true;
  }
  CHECK(hasGoogle);
  CHECK(hasGithub);

  // A second GitHub sign-in is idempotent rather than a conflict.
  Flow github2 = startFlow(api, "github");
  HttpResponse third = callback(api, "github", github2, github2.cookie);
  REQUIRE(third.status == 200);
  CHECK(parseJson(third.body).at("player").s("id") == playerId);
}
