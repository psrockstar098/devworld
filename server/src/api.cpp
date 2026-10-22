#include "api.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

#include "crypto.h"
#include "json.h"
#include "ledger.h"
#include "mastery.h"
#include "progression.h"
#include "skills.h"
#include "util.h"

namespace dw {
namespace {

constexpr size_t kMaxFiles = 20;
constexpr size_t kMaxFileBytes = 200 * 1024;
constexpr size_t kMaxTotalBytes = 1024 * 1024;

HttpResponse jsonRes(const JVal& v, int status = 200) {
  return HttpResponse::json(dumpJson(v), status);
}

bool readObject(const HttpRequest& req, JVal* out, HttpResponse* err) {
  if (req.body.empty()) {
    *err = HttpResponse::error(400, "body must be a JSON object");
    return false;
  }
  try {
    *out = parseJson(req.body);
  } catch (...) {
    *err = HttpResponse::error(400, "body must be a JSON object");
    return false;
  }
  if (out->type != JVal::OBJ) {
    *err = HttpResponse::error(400, "body must be a JSON object");
    return false;
  }
  return true;
}

bool safeFilePath(const std::string& p) {
  if (p.empty() || p.size() > 128) return false;
  if (p[0] == '/') return false;
  if (p.find("..") != std::string::npos) return false;
  for (char c : p) {
    if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' ||
          c == '/'))
      return false;
  }
  return true;
}

bool readColor(const JVal& v, JVal* out) {
  if (v.type != JVal::ARR || v.arr.size() != 3) return false;
  JVal c = jarr();
  for (const auto& ch : v.arr) {
    if (ch.type != JVal::NUM) return false;
    int n = (int)ch.num;
    if (n < 0 || n > 255) return false;
    c.arr.push_back(jnum(n));
  }
  *out = std::move(c);
  return true;
}

std::string hashSubmission(std::vector<AssessFile> files) {
  std::sort(files.begin(), files.end(),
            [](const AssessFile& a, const AssessFile& b) { return a.path < b.path; });
  std::string data;
  for (const auto& f : files) {
    data += f.path;
    data.push_back('\0');
    data += f.content;
    data.push_back('\n');
  }
  return sha256Hex(data).substr(0, 16);
}

bool matchId(const std::string& path, const std::string& prefix,
             const std::string& suffix, std::string* id) {
  if (path.size() <= prefix.size() + suffix.size()) return false;
  if (path.compare(0, prefix.size(), prefix) != 0) return false;
  if (path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)
    return false;
  *id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
  return !id->empty();
}

std::string dummyHash() {
  static const std::string h = passwordHash("not-a-real-password", 120000);
  return h;
}

std::string htmlEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

// JSON literal that is safe to inline inside a <script> block.
std::string inlineJson(std::string out) {
  std::string safe;
  safe.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '<' && i + 1 < out.size() &&
        (out[i + 1] == '/' || out[i + 1] == '!')) {
      safe += "\\u003c";
    } else if (i + 2 < out.size() &&
               (unsigned char)out[i] == 0xe2 &&
               (unsigned char)out[i + 1] == 0x80 &&
               ((unsigned char)out[i + 2] == 0xa8 ||
                (unsigned char)out[i + 2] == 0xa9)) {
      // U+2028/U+2029 are valid JSON but line terminators in older JS.
      safe += (unsigned char)out[i + 2] == 0xa8 ? "\\u2028" : "\\u2029";
      i += 2;
    } else {
      safe += out[i];
    }
  }
  return safe;
}

std::string scriptJson(const std::string& s) {
  return inlineJson(dumpJson(jstr(s)));
}

// Provider accents come from configuration and end up in a CSS custom
// property, so only accept a plain hex colour.
std::string safeBrand(const std::string& brand) {
  if (brand.size() != 4 && brand.size() != 7 && brand.size() != 9) return "";
  if (brand[0] != '#') return "";
  for (size_t i = 1; i < brand.size(); ++i)
    if (!std::isxdigit((unsigned char)brand[i])) return "";
  return brand;
}

}  // namespace

Api::Api(History* history, KvStore* kv, KvStore* cache, PlayerStore* players,
         SessionStore* sessions, MissionCatalog* missions, AssessClient* assess,
         ApiConfig cfg, const MarketCatalog* markets, const Curriculum* curriculum)
    : history_(history), kv_(kv), cache_(cache), players_(players),
      sessions_(sessions), missions_(missions), assess_(assess),
      cfg_(std::move(cfg)),
      ipLimiter_(cfg_.authRateLimitWindowSec * 1000, cfg_.authRateLimitMaxPerIp),
      accountLimiter_(cfg_.authRateLimitWindowSec * 1000, cfg_.authRateLimitMaxPerAccount),
      oauth_(cfg_.oauth, cache, players, sessions),
      friends_(kv), presence_(cache), events_(kv), markets_(kv),
      marketCatalog_(markets), curriculum_(curriculum) {}

Api::~Api() = default;

std::string Api::tokenOf(const HttpRequest& req) const {
  std::string auth = req.header("authorization");
  std::string lowerAuth = toLower(auth);
  if (lowerAuth.rfind("bearer ", 0) == 0) return trim(auth.substr(7));
  return "";
}

Api::Auth Api::requirePlayer(const HttpRequest& req) {
  Auth a;
  std::string token = tokenOf(req);
  if (token.empty()) {
    a.response = HttpResponse::error(401, "unauthorized");
    return a;
  }
  std::string pid;
  bool found = false;
  std::string err;
  if (!sessions_->resolve(token, &pid, &found, &err)) {
    a.response = HttpResponse::error(500, "session store unavailable");
    return a;
  }
  if (!found) {
    a.response = HttpResponse::error(401, "unauthorized");
    return a;
  }
  sessions_->touch(token, &err);  // sliding expiry; failure is not fatal
  a.ok = true;
  a.playerId = pid;
  return a;
}

bool Api::balanceOf(const std::string& playerId, int64_t* out, std::string* err) {
  std::string key = "balance:" + playerId + ":credits";
  std::string cached;
  bool found = false;
  if (cache_->get(key, &cached, &found, err) && found) {
    int64_t v = 0;
    if (parseInt64(cached, &v)) {
      *out = v;
      return true;
    }
  }
  if (!history_->playerBalance(playerId, "credits", out, err)) return false;
  cache_->set(key, std::to_string(*out), cfg_.balanceCacheTtlSec, err);
  return true;
}

bool Api::totalXpOf(const std::string& playerId, int64_t* out, std::string* err) {
  return history_->playerTotalXp(playerId, out, err);
}

HttpResponse Api::handle(const HttpRequest& req) {
  HttpResponse res;
  if (req.method == "OPTIONS") {
    res.status = 204;
  } else {
    res = dispatch(req);
  }
  res.headers["access-control-allow-origin"] = "*";
  res.headers["access-control-allow-headers"] =
      "Authorization, Content-Type, X-Admin-Key";
  res.headers["access-control-allow-methods"] = "GET, POST, PUT, DELETE, OPTIONS";
  res.headers["access-control-max-age"] = "86400";
  return res;
}

HttpResponse Api::dispatch(const HttpRequest& req) {
  try {
    const std::string& path = req.path;
    const std::string& method = req.method;

    if (path == "/" && method == "GET") {
      JVal v = jobj();
      jset(v, "service", jstr("devworld-game-server"));
      jset(v, "version", jstr("0.2.0"));
      jset(v, "runtime", jstr("c++"));
      jset(v, "missions", jnum((double)missions_->size()));
      return jsonRes(v);
    }
    if (path == "/health" && method == "GET") return handleHealth();
    if (path == "/auth/register" && method == "POST") return handleRegister(req);
    if (path == "/auth/login" && method == "POST") return handleLogin(req);
    if (path == "/auth/logout" && method == "POST") return handleLogout(req);
    if (path == "/auth/providers" && method == "GET")
      return handleOAuthProviders();
    std::string oauthProvider;
    if (method == "GET" &&
        matchId(path, "/auth/oauth/", "/start", &oauthProvider))
      return handleOAuthStart(req, oauthProvider);
    if (method == "GET" &&
        matchId(path, "/auth/oauth/", "/callback", &oauthProvider))
      return handleOAuthCallback(req, oauthProvider);
    if ((path == "/me" || path == "/progression/me") && method == "GET")
      return handleMe(req);
    if (path == "/missions" && method == "GET") return handleMissions();
    if (path == "/stats/missions" && method == "GET") return handleMissionStats();
    if (path == "/skills/evidence" && method == "POST")
      return handleRecordEvidence(req);
    if (path == "/skills/me" && method == "GET") return handleSkillsMe(req);
    if (path == "/skills/trees" && method == "GET") return handleSkillsTrees();
    if (path == "/ledger/balance" && method == "GET") return handleBalance(req);
    if (path == "/admin/ledger" && method == "POST") return handleAdminLedger(req);
    if (path == "/events" && method == "POST") return handleRecordEvent(req);
    if (path == "/events" && method == "GET") return handleListEvents(req);
    if (path == "/market" && method == "GET") return handleMarketList(req);
    if (path == "/market/buy" && method == "POST") return handleMarketBuy(req);
    if (path == "/market/inventory" && method == "GET") return handleMarketInventory(req);
    if (path == "/market/restock" && method == "POST") return handleMarketRestock(req);
    if (path == "/me/appearance" && (method == "PUT" || method == "POST"))
      return handleUpdateAppearance(req);
    if (path == "/world/players" && method == "GET")
      return handleWorldPlayers(req);
    if (path == "/friends" && method == "GET") return handleFriends(req);
    if (path == "/friends/request" && method == "POST")
      return handleFriendRequest(req);
    if (path == "/friends/accept" && method == "POST")
      return handleFriendAccept(req);
    if (path == "/friends/decline" && method == "POST")
      return handleFriendDecline(req);
    std::string friendId;
    if (method == "DELETE" && matchId(path, "/friends/", "", &friendId))
      return handleFriendRemove(friendId, req);
    if (path == "/ws" && method == "GET") return wsHandshake(req);

    std::string id;
    if (method == "GET" && matchId(path, "/players/", "", &id))
      return handleGetPlayer(id);
    if (method == "POST" && matchId(path, "/missions/", "/submit", &id))
      return handleSubmit(req, id);

    return HttpResponse::error(404, "not found");
  } catch (const std::exception&) {
    return HttpResponse::error(500, "internal error");
  } catch (...) {
    return HttpResponse::error(500, "internal error");
  }
}

HttpResponse Api::handleHealth() {
  std::string err;
  bool ch = history_->ping();
  bool kv = kv_->ping(&err);
  bool redis = cache_->ping(&err);
  bool ok = ch && kv && redis;
  JVal stores = jobj();
  jset(stores, "clickhouse", jbool(ch));
  jset(stores, "kvrocks", jbool(kv));
  jset(stores, "redis", jbool(redis));
  JVal v = jobj();
  jset(v, "ok", jbool(ok));
  jset(v, "stores", stores);
  jset(v, "missions", jnum((double)missions_->size()));
  jset(v, "websockets", jnum((double)hub_.size()));
  return jsonRes(v, ok ? 200 : 503);
}

bool Api::authAllowed(const HttpRequest& req, const std::string& account,
                      HttpResponse* limited) {
  int retryAfter = 0;
  std::string ip = clientIpFromPeer(req.peer);
  if (ip.empty()) ip = "unknown";
  if (!ipLimiter_.allow("ip:" + ip, &retryAfter) ||
      (!account.empty() && !accountLimiter_.allow("account:" + account, &retryAfter))) {
    *limited = HttpResponse::error(429, "too many attempts, try again later");
    limited->headers["retry-after"] = std::to_string(retryAfter > 0 ? retryAfter : 1);
    return false;
  }
  return true;
}

HttpResponse Api::handleRegister(const HttpRequest& req) {
  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

  HttpResponse limited;
  if (!authAllowed(req, body.s("username"), &limited)) return limited;

  std::string username = body.s("username");
  std::string password = body.s("password");
  std::string display = body.s("displayName");

  PlayerRecord rec;
  int status = 0;
  std::string err;
  if (!players_->create(username, password, display, &rec, &status, &err))
    return HttpResponse::error(status ? status : 500, err);

  // Every account owns a unit in the shared Home Row. The counter lives in
  // Redis so units are never reused, and the number is part of the account.
  ensureHome(&rec);

  IssuedSession issued;
  if (!sessions_->create(rec.id, &issued, &err))
    return HttpResponse::error(500, "could not create session");

  history_->insertEvents(
      {{rec.id, "player.registered", "", "game-server", "{}"}}, &err);

  // Play writes its own history: a new resident joins the world.
  WorldEvent joined;
  joined.type = "player.joined";
  joined.title = rec.handle + " joined the world";
  joined.actorId = rec.id;
  events_.record(joined, nullptr, &err);

  JVal v = jobj();
  jset(v, "token", jstr(issued.token));
  jset(v, "expiresInSec", jnum(issued.expiresInSec));
  jset(v, "player", rec.publicJson());
  return jsonRes(v, 201);
}

HttpResponse Api::handleLogin(const HttpRequest& req) {
  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

  HttpResponse limited;
  if (!authAllowed(req, body.s("username"), &limited)) return limited;

  std::string username = body.s("username");
  std::string password = body.s("password");

  std::string err;
  PlayerRecord rec;
  bool found = false;

  // Password login only: a bare player id must never issue a session.
  if (username.empty() || password.empty())
    return HttpResponse::error(400, "username and password are required");
  if (!players_->getByUsername(username, &rec, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  // Always run the KDF so a missing username is not a timing oracle.
  const std::string& hash = found ? rec.passwordHash : dummyHash();
  bool valid = passwordVerify(password, hash);
  if (!found || !valid)
    return HttpResponse::error(401, "invalid credentials");

  // Accounts created before Home Row existed get a unit on first login.
  ensureHome(&rec);

  IssuedSession issued;
  if (!sessions_->create(rec.id, &issued, &err))
    return HttpResponse::error(500, "could not create session");

  JVal v = jobj();
  jset(v, "token", jstr(issued.token));
  jset(v, "expiresInSec", jnum(issued.expiresInSec));
  jset(v, "player", rec.publicJson());
  return jsonRes(v);
}

HttpResponse Api::handleLogout(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  std::string err;
  sessions_->destroy(tokenOf(req), &err);
  JVal v = jobj();
  jset(v, "ok", jbool(true));
  return jsonRes(v);
}

HttpResponse Api::handleOAuthProviders() {
  JVal providers = jarr();
  for (const auto& p : oauth_.providers()) {
    if (!p.configured()) continue;
    JVal one = jobj();
    jset(one, "id", jstr(p.id));
    jset(one, "name", jstr(p.name));
    std::string brand = safeBrand(p.brand);
    jset(one, "brand", jstr(brand.empty() ? "#64748b" : brand));
    // Absolute: the callback and its state cookie must live on the configured
    // public origin, even when the panel itself is served from another host.
    jset(one, "startUrl",
         jstr(cfg_.oauth.baseUrl + "/auth/oauth/" + p.id + "/start"));
    providers.arr.push_back(std::move(one));
  }
  JVal v = jobj();
  jset(v, "providers", std::move(providers));
  jset(v, "configured", jbool(oauth_.configured()));
  HttpResponse res = jsonRes(v);
  res.headers["cache-control"] = "no-store";
  return res;
}

std::string Api::cookieOf(const HttpRequest& req, const std::string& name) const {
  std::string raw = req.header("cookie");
  for (const auto& part : split(raw, ';')) {
    std::string item = trim(part);
    size_t eq = item.find('=');
    if (eq == std::string::npos) continue;
    if (trim(item.substr(0, eq)) == name) return trim(item.substr(eq + 1));
  }
  return "";
}

HttpResponse Api::oauthFailure(const HttpRequest& req, int status,
                               const std::string& message, bool json) {
  (void)req;
  HttpResponse res;
  if (json) {
    res = HttpResponse::error(status, message);
  } else {
    res.status = status;
    res.headers["content-type"] = "text/html; charset=utf-8";
    res.body =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>DEVWORLD sign-in failed</title>"
        "<style>body{margin:0;min-height:100vh;display:grid;place-items:center;"
        "background:#081016;color:#dbe7ea;font:15px/1.5 system-ui,sans-serif}"
        ".box{max-width:420px;padding:28px;border:1px solid #1e303a;"
        "background:#0b1319}h1{font-size:18px;margin:0 0 10px}"
        "p{color:#93a7ad;margin:0 0 16px}button{font:inherit;padding:8px 14px;"
        "background:#1e303a;color:#dbe7ea;border:1px solid #2b4652;"
        "cursor:pointer}</style></head><body><div class=\"box\">"
        "<h1>Sign-in failed</h1><p>" +
        htmlEscape(message) +
        "</p><button onclick=\"window.close()\">Close window</button>"
        "</div><script>try{if(window.opener&&!window.opener.closed){"
        "window.opener.postMessage({type:'devworld-oauth',error:" +
        scriptJson(message) + "},'*');}}catch(e){}</script>"
        "</body></html>";
  }
  res.headers["cache-control"] = "no-store";
  res.headers["referrer-policy"] = "no-referrer";
  res.headers["content-security-policy"] =
      "default-src 'none'; style-src 'unsafe-inline'; script-src "
      "'unsafe-inline'; base-uri 'none'; form-action 'none'";
  res.headers["set-cookie"] =
      std::string("dw_oauth_state=; Path=/auth/oauth; HttpOnly; SameSite=Lax; "
                  "Max-Age=0") +
      (startsWith(toLower(cfg_.oauth.baseUrl), "https://") ? "; Secure" : "");
  return res;
}

HttpResponse Api::handleOAuthStart(const HttpRequest& req,
                                   const std::string& provider) {
  bool json = toLower(req.param("format")) == "json";
  std::string returnTo = req.param("returnTo");
  if (returnTo.empty()) returnTo = req.param("redirect");

  std::string url, state, err;
  int status = 500;
  if (!oauth_.start(provider, returnTo, json, &url, &state, &status, &err))
    return oauthFailure(req, status, err, json);

  HttpResponse res;
  res.status = 302;
  res.headers["location"] = url;
  res.headers["cache-control"] = "no-store";
  res.headers["referrer-policy"] = "no-referrer";
  res.headers["set-cookie"] =
      "dw_oauth_state=" + state + "; Path=/auth/oauth; HttpOnly; SameSite=Lax; "
      "Max-Age=" + std::to_string(cfg_.oauth.stateTtlSec) +
      (startsWith(toLower(cfg_.oauth.baseUrl), "https://") ? "; Secure" : "");
  return res;
}

HttpResponse Api::oauthSuccessPage(const OAuthResult& result,
                                   const IssuedSession& session) {
  std::string origin;
  cfg_.oauth.returnToAllowed(result.returnTo, &origin);
  JVal payload = jobj();
  jset(payload, "type", jstr("devworld-oauth"));
  jset(payload, "token", jstr(session.token));
  jset(payload, "provider", jstr(result.provider));
  jset(payload, "providerName", jstr(result.providerName));
  std::string payloadJson = inlineJson(dumpJson(payload));

  std::string body =
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>DEVWORLD sign-in</title>"
      "<style>body{margin:0;min-height:100vh;display:grid;place-items:center;"
      "background:#081016;color:#dbe7ea;font:15px/1.5 system-ui,sans-serif}"
      ".box{max-width:420px;padding:28px;border:1px solid #1e303a;"
      "background:#0b1319}h1{font-size:18px;margin:0 0 10px}"
      "p{color:#93a7ad;margin:0}</style></head><body><div class=\"box\">"
      "<h1>Signed in</h1><p id=\"msg\">Returning to DEVWORLD…</p></div>"
      "<script>(function(){var payload=" +
      payloadJson + ";var returnTo=" + scriptJson(result.returnTo) +
      ";var target=" + scriptJson(origin) +
      ";try{if(window.opener&&!window.opener.closed&&target){"
      "window.opener.postMessage(payload,target);"
      "setTimeout(function(){window.close();},80);return;}}catch(e){}"
      "if(returnTo){var sep=returnTo.indexOf('#')>=0?'&':'#';"
      "location.replace(returnTo+sep+'oauth_token='+encodeURIComponent("
      "payload.token));return;}"
      "document.getElementById('msg').textContent='Signed in. You can close "
      "this window.';})();</script></body></html>";

  HttpResponse res;
  res.status = 200;
  res.headers["content-type"] = "text/html; charset=utf-8";
  res.headers["cache-control"] = "no-store";
  res.headers["referrer-policy"] = "no-referrer";
  res.headers["content-security-policy"] =
      "default-src 'none'; style-src 'unsafe-inline'; script-src "
      "'unsafe-inline'; base-uri 'none'; form-action 'none'";
  res.headers["set-cookie"] =
      std::string("dw_oauth_state=; Path=/auth/oauth; HttpOnly; SameSite=Lax; "
                  "Max-Age=0") +
      (startsWith(toLower(cfg_.oauth.baseUrl), "https://") ? "; Secure" : "");
  res.body = std::move(body);
  return res;
}

HttpResponse Api::handleOAuthCallback(const HttpRequest& req,
                                      const std::string& provider) {
  bool json = toLower(req.param("format")) == "json";
  std::string providerError = req.param("error");
  if (!providerError.empty()) {
    std::string detail = req.param("error_description");
    if (detail.empty()) detail = providerError;
    return oauthFailure(req, 400, "Sign-in was cancelled: " + detail, json);
  }

  OAuthResult result;
  int status = 400;
  std::string err;
  if (!oauth_.complete(provider, req.param("code"), req.param("state"),
                       cookieOf(req, "dw_oauth_state"), &result, &status, &err))
    return oauthFailure(req, status, err, json);

  json = json || result.json;
  ensureHome(&result.player);

  IssuedSession session;
  if (!sessions_->create(result.player.id, &session, &err))
    return oauthFailure(req, 503, "could not create session", json);

  JVal props = jobj();
  jset(props, "provider", jstr(result.provider));
  jset(props, "created", jbool(result.created));
  history_->insertEvents({{result.player.id,
                           result.created ? "player.registered"
                                          : "player.oauth_login",
                           "", "game-server", dumpJson(props)}},
                         &err);

  if (result.created) {
    WorldEvent joined;
    joined.type = "player.joined";
    joined.title = result.player.handle + " joined the world";
    joined.actorId = result.player.id;
    events_.record(joined, nullptr, &err);
  }

  if (json) {
    JVal v = jobj();
    jset(v, "token", jstr(session.token));
    jset(v, "expiresInSec", jnum(session.expiresInSec));
    jset(v, "player", result.player.selfJson());
    jset(v, "provider", jstr(result.provider));
    jset(v, "providerName", jstr(result.providerName));
    jset(v, "created", jbool(result.created));
    HttpResponse res = jsonRes(v);
    res.headers["cache-control"] = "no-store";
    res.headers["set-cookie"] =
        std::string("dw_oauth_state=; Path=/auth/oauth; HttpOnly; SameSite=Lax; "
                    "Max-Age=0") +
        (startsWith(toLower(cfg_.oauth.baseUrl), "https://") ? "; Secure" : "");
    return res;
  }
  return oauthSuccessPage(result, session);
}

HttpResponse Api::handleMe(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  PlayerRecord rec;
  bool found = false;
  std::string err;
  if (!players_->get(a.playerId, &rec, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found) return HttpResponse::error(404, "no such player");
  ensureHome(&rec);

  int64_t xp = 0;
  int64_t balance = 0;
  if (!totalXpOf(a.playerId, &xp, &err))
    return HttpResponse::error(503, "history store unavailable");
  if (!balanceOf(a.playerId, &balance, &err))
    return HttpResponse::error(503, "history store unavailable");

  JVal v = rec.selfJson();
  jset(v, "progression", progressionView(xp));
  jset(v, "balance", jnum((double)balance));
  return jsonRes(v);
}

HttpResponse Api::handleGetPlayer(const std::string& id) {
  PlayerRecord rec;
  bool found = false;
  std::string err;
  if (!players_->get(id, &rec, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found) return HttpResponse::error(404, "no such player");
  JVal v = rec.publicJson();
  jset(v, "online", jbool(presence_.isOnline(rec.id)));
  return jsonRes(v);
}

void Api::ensureHome(PlayerRecord* rec) {
  if (rec->homeUnit > 0) return;
  int64_t unit = 0;
  std::string err;
  if (!cache_->incr("world:home:cursor", 1, &unit, &err) || unit <= 0) {
    // Cache down: derive a stable unit from the account id so the player still
    // has a home and the same one on the next login.
    unit = 1 + (int64_t)(std::hash<std::string>{}(rec->id) % 900);
  }
  rec->homeUnit = (int)unit;
  rec->updatedAt = isoNow();
  players_->save(*rec, &err);
}

JVal Api::friendView(const std::string& playerId) {
  PlayerRecord rec;
  bool found = false;
  std::string err;
  JVal v = jobj();
  if (players_->get(playerId, &rec, &found, &err) && found) {
    v = rec.publicJson();
  } else {
    jset(v, "id", jstr(playerId));
    jset(v, "handle", jstr(playerId));
    jset(v, "displayName", jstr(playerId));
    jset(v, "appearance", jobj());
  }
  jset(v, "online", jbool(presence_.isOnline(playerId)));
  return v;
}

HttpResponse Api::handleUpdateAppearance(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

  PlayerRecord rec;
  bool found = false;
  std::string err;
  if (!players_->get(a.playerId, &rec, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found) return HttpResponse::error(404, "no such player");

  JVal appearance =
      rec.appearance.type == JVal::OBJ ? rec.appearance : jobj();
  bool changed = false;
  for (const char* key : {"skin", "shirt", "pants", "hair"}) {
    const JVal& value = body.at(key);
    if (value.type == JVal::NUL) continue;
    JVal color;
    if (!readColor(value, &color))
      return HttpResponse::error(
          400, std::string(key) + " must be [r,g,b] with values 0-255");
    jset(appearance, key, std::move(color));
    changed = true;
  }
  if (!changed) return HttpResponse::error(400, "no colors supplied");

  rec.appearance = appearance;
  rec.updatedAt = isoNow();
  if (!players_->save(rec, &err))
    return HttpResponse::error(503, "player store unavailable");

  // Anyone looking at the world or a friend list sees the new look live.
  JVal ev = jobj();
  jset(ev, "type", jstr("player.updated"));
  jset(ev, "player", rec.publicJson());
  hub_.broadcast(dumpJson(ev));

  JVal v = jobj();
  jset(v, "player", rec.publicJson());
  return jsonRes(v);
}

HttpResponse Api::handleWorldPlayers(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  JVal arr = jarr();
  auto entries = presence_.list();
  for (const auto& e : entries) arr.arr.push_back(e.toJson());

  JVal v = jobj();
  jset(v, "players", std::move(arr));
  jset(v, "count", jnum((double)entries.size()));
  jset(v, "serverTime", jstr(isoNow()));
  return jsonRes(v);
}

HttpResponse Api::handleFriends(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  FriendLinks links;
  std::string err;
  if (!friends_.get(a.playerId, &links, &err))
    return HttpResponse::error(503, "friend store unavailable");

  auto view = [&](const std::vector<std::string>& ids) {
    JVal arr = jarr();
    for (const auto& id : ids) arr.arr.push_back(friendView(id));
    return arr;
  };

  JVal v = jobj();
  jset(v, "friends", view(links.friends));
  jset(v, "incoming", view(links.incoming));
  jset(v, "outgoing", view(links.outgoing));
  return jsonRes(v);
}

HttpResponse Api::handleFriendRequest(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  std::string target = body.s("playerId");
  if (target.empty()) target = body.s("handle");
  if (target.empty()) return HttpResponse::error(400, "playerId is required");

  PlayerRecord them;
  bool found = false;
  std::string err;
  if (!players_->get(target, &them, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found && !players_->getByHandle(target, &them, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found) return HttpResponse::error(404, "no such player");
  if (them.id == a.playerId)
    return HttpResponse::error(400, "cannot befriend yourself");

  int status = 0;
  if (!friends_.request(a.playerId, them.id, &status, &err))
    return HttpResponse::error(503, "friend store unavailable");
  if (status != 200) return HttpResponse::error(status, err);

  PlayerRecord me;
  if (players_->get(a.playerId, &me, &found, &err) && found) {
    JVal ev = jobj();
    jset(ev, "type", jstr("friend.request"));
    jset(ev, "from", me.publicJson());
    hub_.sendTo(them.id, dumpJson(ev));
  }

  JVal v = jobj();
  jset(v, "ok", jbool(true));
  jset(v, "status", jstr("pending"));
  return jsonRes(v);
}

HttpResponse Api::handleFriendAccept(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  std::string requester = body.s("playerId");
  if (requester.empty()) return HttpResponse::error(400, "playerId is required");

  int status = 0;
  std::string err;
  if (!friends_.accept(a.playerId, requester, &status, &err))
    return HttpResponse::error(503, "friend store unavailable");
  if (status != 200) return HttpResponse::error(status, err);

  PlayerRecord me, them;
  bool found = false;
  if (players_->get(a.playerId, &me, &found, &err) && found) {
    JVal ev = jobj();
    jset(ev, "type", jstr("friend.accepted"));
    jset(ev, "player", me.publicJson());
    hub_.sendTo(requester, dumpJson(ev));
  }
  if (players_->get(requester, &them, &found, &err) && found) {
    JVal ev = jobj();
    jset(ev, "type", jstr("friend.accepted"));
    jset(ev, "player", them.publicJson());
    hub_.sendTo(a.playerId, dumpJson(ev));
  }

  JVal v = jobj();
  jset(v, "ok", jbool(true));
  return jsonRes(v);
}

HttpResponse Api::handleFriendDecline(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  std::string requester = body.s("playerId");
  if (requester.empty()) return HttpResponse::error(400, "playerId is required");

  int status = 0;
  std::string err;
  if (!friends_.decline(a.playerId, requester, &status, &err))
    return HttpResponse::error(503, "friend store unavailable");
  if (status != 200) return HttpResponse::error(status, err);

  JVal ev = jobj();
  jset(ev, "type", jstr("friend.declined"));
  jset(ev, "playerId", jstr(a.playerId));
  hub_.sendTo(requester, dumpJson(ev));

  JVal v = jobj();
  jset(v, "ok", jbool(true));
  return jsonRes(v);
}

HttpResponse Api::handleFriendRemove(const std::string& id,
                                     const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  if (id == a.playerId)
    return HttpResponse::error(400, "cannot remove yourself");

  int status = 0;
  std::string err;
  if (!friends_.remove(a.playerId, id, &status, &err))
    return HttpResponse::error(503, "friend store unavailable");
  if (status != 200) return HttpResponse::error(status, err);

  JVal ev = jobj();
  jset(ev, "type", jstr("friend.removed"));
  jset(ev, "playerId", jstr(a.playerId));
  hub_.sendTo(id, dumpJson(ev));

  JVal v = jobj();
  jset(v, "ok", jbool(true));
  return jsonRes(v);
}

HttpResponse Api::handleRecordEvent(const HttpRequest& req) {
  if (cfg_.adminKey.empty()) return HttpResponse::error(404, "not found");
  if (req.header("x-admin-key") != cfg_.adminKey)
    return HttpResponse::error(401, "unauthorized");

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

  WorldEvent ev;
  ev.type = body.s("type");
  ev.title = body.s("title");
  if (body.at("detail").type == JVal::STR) ev.detail = body.at("detail").str;
  if (body.at("actorId").type == JVal::STR) ev.actorId = body.at("actorId").str;
  if (body.at("city").type == JVal::STR) ev.city = body.at("city").str;

  if (!validEventType(ev.type))
    return HttpResponse::error(400,
                               "type must be 1-64 chars [A-Za-z0-9_.-]");
  if (ev.title.empty() || ev.title.size() > 200)
    return HttpResponse::error(400, "title must be 1-200 chars");
  if (ev.detail.size() > 2000)
    return HttpResponse::error(400, "detail must be at most 2000 chars");
  if (ev.actorId.size() > 64)
    return HttpResponse::error(400, "actorId must be at most 64 chars");
  if (ev.city.size() > 64)
    return HttpResponse::error(400, "city must be at most 64 chars");

  WorldEvent saved;
  std::string err;
  if (!events_.record(ev, &saved, &err))
    return HttpResponse::error(503, "event store unavailable");
  return jsonRes(saved.toJson(), 201);
}

HttpResponse Api::handleListEvents(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  std::string type = req.param("type");
  if (!type.empty() && !validEventType(type))
    return HttpResponse::error(400,
                               "type must be 1-64 chars [A-Za-z0-9_.-]");

  int limit = EventStore::kDefaultList;
  std::string limitRaw = req.param("limit");
  if (!limitRaw.empty()) {
    char* end = nullptr;
    double n = std::strtod(limitRaw.c_str(), &end);
    if (end == limitRaw.c_str() || *end != '\0' || !std::isfinite(n) || n < 1)
      return HttpResponse::error(400, "limit must be a positive number");
    limit = static_cast<int>(n);
  }

  std::vector<WorldEvent> events;
  std::string err;
  if (!events_.list(type, limit, &events, &err))
    return HttpResponse::error(503, "event store unavailable");

  JVal arr = jarr();
  for (const WorldEvent& ev : events) arr.arr.push_back(ev.toJson());
  JVal v = jobj();
  jset(v, "events", arr);
  return jsonRes(v);
}

HttpResponse Api::handleMissions() { return jsonRes(missions_->listJson()); }

// POST /skills/evidence — record one mastery observation for the caller.
// The caller is always the authenticated player: a body playerId, if any,
// is ignored. Score must be present and inside [0,1] — parseEvidence
// clamps, and silently clamping a client bug into a perfect score would be
// worse than rejecting it.
HttpResponse Api::handleRecordEvidence(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  JVal body;
  HttpResponse errRes;
  if (!readObject(req, &body, &errRes)) return errRes;
  std::string skill = body.s("skill");
  if (skill.empty()) return HttpResponse::error(400, "skill is required");
  EvidenceKind kind = EvidenceKind::AutomatedTest;
  if (!evidenceKindFromString(body.s("kind"), kind))
    return HttpResponse::error(400, "unknown evidence kind: " + body.s("kind"));
  if (body.at("score").type != JVal::NUM)
    return HttpResponse::error(400, "score is required");
  double score = body.n("score", 0);
  if (score < 0 || score > 1)
    return HttpResponse::error(400, "score must be between 0 and 1");
  int64_t atMs = (int64_t)body.n("atMs", 0);
  if (atMs <= 0) atMs = nowMs();
  Evidence e = makeEvidence(a.playerId, skill, kind, score, atMs,
                            body.s("source"), body.s("detail"));
  std::vector<Evidence> updated;
  bool duplicate = false;
  std::string err;
  if (!recordEvidence(kv_, e, &duplicate, &updated, &err))
    return HttpResponse::error(503, "evidence store unavailable");
  MasteryConfig cfg;
  JVal v = skillMasteryJson(
      summarizeSkill(skill, updated, cfg, nowMs()));
  jset(v, "duplicate", jbool(duplicate));
  return jsonRes(v, duplicate ? 200 : 201);
}

HttpResponse Api::handleSkillsMe(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  std::vector<Evidence> all;
  std::string err;
  if (!loadEvidence(kv_, a.playerId, &all, &err))
    return HttpResponse::error(503, "evidence store unavailable");
  MasteryConfig cfg;
  return jsonRes(skillsMeJson(a.playerId, all, cfg, nowMs()));
}

// GET /skills/trees — the skill tree: areas with their topics, in catalog
// order. Public catalog data, like /missions: no player state, no auth.
// Objectives and misconceptions are authoring detail, not tree structure,
// so they stay out of this response.
HttpResponse Api::handleSkillsTrees() {
  if (!curriculum_ || curriculum_->empty())
    return HttpResponse::error(503, "curriculum unavailable");
  JVal areas = jarr();
  for (const auto& area : curriculum_->areas()) {
    JVal a = jobj();
    jset(a, "id", jstr(area.id));
    jset(a, "title", jstr(area.title));
    jset(a, "summary", jstr(area.summary));
    JVal topics = jarr();
    for (const auto* t : curriculum_->topicsInArea(area.id)) {
      JVal tj = jobj();
      jset(tj, "id", jstr(t->id));
      jset(tj, "title", jstr(t->title));
      jset(tj, "summary", jstr(t->summary));
      jset(tj, "difficulty", jnum(t->difficulty));
      JVal prereqs = jarr();
      for (const auto& p : t->prerequisites) prereqs.arr.push_back(jstr(p));
      jset(tj, "prerequisites", prereqs);
      JVal missions = jarr();
      for (const auto& m : t->missions) missions.arr.push_back(jstr(m));
      jset(tj, "missions", missions);
      JVal projects = jarr();
      for (const auto& p : t->projects) projects.arr.push_back(jstr(p));
      jset(tj, "projects", projects);
      topics.arr.push_back(tj);
    }
    jset(a, "topics", topics);
    areas.arr.push_back(a);
  }
  JVal out = jobj();
  jset(out, "areas", areas);
  return jsonRes(out);
}

HttpResponse Api::handleMissionStats() {
  std::vector<MissionStat> stats;
  std::string err;
  if (!history_->missionStats(&stats, &err))
    return HttpResponse::error(503, "history store unavailable");
  JVal arr = jarr();
  for (const auto& s : stats) {
    JVal v = jobj();
    jset(v, "missionId", jstr(s.missionId));
    jset(v, "attempts", jnum((double)s.attempts));
    jset(v, "passes", jnum((double)s.passes));
    arr.arr.push_back(std::move(v));
  }
  JVal out = jobj();
  jset(out, "missions", arr);
  return jsonRes(out);
}

HttpResponse Api::handleSubmit(const HttpRequest& req, const std::string& id) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;

  const MissionDef* mission = missions_->get(id);
  if (!mission) return HttpResponse::error(404, "no such mission");

  PlayerRecord rec;
  bool found = false;
  std::string err;
  if (!players_->get(a.playerId, &rec, &found, &err))
    return HttpResponse::error(500, "player store unavailable");
  if (!found) return HttpResponse::error(404, "no such player");

  if (std::find(rec.completedMissions.begin(), rec.completedMissions.end(),
                mission->id) != rec.completedMissions.end())
    return HttpResponse::error(409, "mission already completed");

  std::string missing;
  for (const auto& r : mission->requires) {
    if (std::find(rec.completedMissions.begin(), rec.completedMissions.end(),
                  r) == rec.completedMissions.end()) {
      if (!missing.empty()) missing += ", ";
      missing += r;
    }
  }
  if (!missing.empty())
    return HttpResponse::error(403, "locked: finish " + missing + " first");

  int64_t totalXpBefore = 0;
  if (!totalXpOf(a.playerId, &totalXpBefore, &err))
    return HttpResponse::error(503, "history store unavailable");
  int level = levelFromTotal(totalXpBefore).level;
  if (level < mission->level)
    return HttpResponse::error(
        403, "locked: requires level " + std::to_string(mission->level));

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

  const JVal& filesJson = body.at("files");
  if (filesJson.type != JVal::ARR || filesJson.arr.empty())
    return HttpResponse::error(400, "files must be a non-empty array");
  if (filesJson.arr.size() > kMaxFiles)
    return HttpResponse::error(400, "at most " + std::to_string(kMaxFiles) + " files");

  std::vector<AssessFile> files;
  std::set<std::string> seen;
  size_t total = 0;
  for (const auto& f : filesJson.arr) {
    AssessFile af;
    af.path = f.s("path");
    af.content = f.s("content");
    if (!safeFilePath(af.path))
      return HttpResponse::error(400, "unsafe file path: " + af.path);
    if (!seen.insert(af.path).second)
      return HttpResponse::error(400, "duplicate file path: " + af.path);
    if (af.content.size() > kMaxFileBytes)
      return HttpResponse::error(413, "file too large: " + af.path);
    total += af.content.size();
    if (total > kMaxTotalBytes)
      return HttpResponse::error(413, "submission too large");
    files.push_back(std::move(af));
  }

  // The worker picks the runtime's conventional entry name (main.py) or the
  // first file it receives. IDE submissions also carry README/scratch files,
  // so put the mission's own file first and the intended entry always runs.
  if (!mission->file.empty()) {
    auto entry = std::find_if(files.begin(), files.end(),
                              [&](const AssessFile& f) {
                                return f.path == mission->file;
                              });
    if (entry != files.end() && entry != files.begin())
      std::rotate(files.begin(), entry, entry + 1);
  }

  if (!assess_ || !assess_->configured())
    return HttpResponse::error(503,
                               "grading backend not configured (set WORKER_URL)");

  AssessResult result;
  std::string aerr;
  if (!assess_->assess(mission->language, files, assessTestsFor(*mission), &result,
                       &aerr))
    return HttpResponse::error(503, aerr);

  MissionGrading grading = summarize(*mission, result);
  std::string codeHash = hashSubmission(files);

  MissionAttemptRow attempt;
  attempt.playerId = a.playerId;
  attempt.missionId = mission->id;
  attempt.runtime = mission->language;
  attempt.codeHash = codeHash;
  attempt.passed = grading.passed;
  attempt.publicPassed = grading.publicPassed;
  attempt.hiddenPassed = grading.hiddenPassed;
  attempt.total = grading.total;
  attempt.durationMs = result.durationMs;
  history_->insertMissionAttempt(attempt, &err);

  JVal props = jobj();
  jset(props, "missionId", jstr(mission->id));
  jset(props, "passed", jbool(grading.passed));
  jset(props, "publicPassed", jnum(grading.publicPassed));
  jset(props, "hiddenPassed", jnum(grading.hiddenPassed));
  history_->insertEvents({{a.playerId, "mission.attempt", "", "game-server",
                           dumpJson(props)}},
                         &err);

  int64_t totalXpAfter = totalXpBefore;
  if (grading.passed) {
    std::string ref =
        "mission:" + mission->id + ":" + a.playerId + ":" +
        std::to_string(nowMs());
    auto entries = payout(a.playerId, ref, mission->creditReward,
                          "reward " + mission->id);
    if (!validateTransaction(entries, &err))
      return HttpResponse::error(500, "invalid reward transaction");
    std::vector<LedgerRow> rows;
    for (const auto& e : entries)
      rows.push_back({e.ref, e.playerId, e.account, e.amount, e.currency, e.memo});
    if (!history_->insertLedger(rows, &err))
      return HttpResponse::error(503, "ledger store unavailable");

    totalXpAfter = totalXpBefore + mission->xpReward;
    if (!history_->insertXp(
            {{a.playerId, "mission", mission->id, mission->xpReward,
              levelFromTotal(totalXpAfter).level}},
            &err))
      return HttpResponse::error(503, "xp store unavailable");
    players_->markCompleted(a.playerId, mission->id, &err);
    rec.completedMissions.push_back(mission->id);
    cache_->del("balance:" + a.playerId + ":credits", &err);

    // The world writes its own history: every first full pass is news.
    WorldEvent completed;
    completed.type = "mission.completed";
    completed.title = rec.handle + " completed " + mission->title;
    completed.actorId = a.playerId;
    completed.detail = "+" + std::to_string(mission->xpReward) + " XP, +" +
                       std::to_string(mission->creditReward) + " credits";
    events_.record(completed, nullptr, &err);
  }

  int64_t balance = 0;
  if (!balanceOf(a.playerId, &balance, &err))
    return HttpResponse::error(503, "history store unavailable");

  JVal v = jobj();
  jset(v, "missionId", jstr(mission->id));
  jset(v, "passed", jbool(grading.passed));
  jset(v, "total", jnum(grading.total));
  jset(v, "publicPassed", jnum(grading.publicPassed));
  jset(v, "hiddenPassed", jnum(grading.hiddenPassed));
  jset(v, "results", grading.results);
  jset(v, "rewarded", jbool(grading.passed));
  jset(v, "progression", progressionView(totalXpAfter));
  jset(v, "balance", jnum((double)balance));

  if (grading.passed) {
    JVal ev = jobj();
    jset(ev, "type", jstr("mission.completed"));
    jset(ev, "missionId", jstr(mission->id));
    jset(ev, "progression", progressionView(totalXpAfter));
    jset(ev, "balance", jnum((double)balance));
    jset(ev, "player", rec.publicJson());
    hub_.sendTo(a.playerId, dumpJson(ev));
  }

  return jsonRes(v);
}

HttpResponse Api::handleBalance(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  int64_t balance = 0;
  std::string err;
  if (!balanceOf(a.playerId, &balance, &err))
    return HttpResponse::error(503, "history store unavailable");
  JVal v = jobj();
  jset(v, "playerId", jstr(a.playerId));
  jset(v, "currency", jstr("credits"));
  jset(v, "balance", jnum((double)balance));
  return jsonRes(v);
}

HttpResponse Api::handleAdminLedger(const HttpRequest& req) {
  if (cfg_.adminKey.empty()) return HttpResponse::error(404, "not found");
  if (req.header("x-admin-key") != cfg_.adminKey)
    return HttpResponse::error(401, "unauthorized");

  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  const JVal& arr = body.at("entries");
  if (arr.type != JVal::ARR) return HttpResponse::error(400, "entries must be an array");

  std::vector<LedgerEntry> entries;
  for (const auto& e : arr.arr) {
    LedgerEntry le;
    le.ref = e.s("ref");
    le.playerId = e.s("playerId");
    le.account = e.s("account");
    le.amount = (int64_t)e.n("amount", 0);
    le.currency = e.s("currency", "credits");
    le.memo = e.s("memo");
    entries.push_back(std::move(le));
  }
  std::string err;
  if (!validateTransaction(entries, &err)) return HttpResponse::error(400, err);

  std::vector<LedgerRow> rows;
  std::set<std::string> players;
  for (const auto& e : entries) {
    rows.push_back({e.ref, e.playerId, e.account, e.amount, e.currency, e.memo});
    players.insert(e.playerId);
  }
  if (!history_->insertLedger(rows, &err))
    return HttpResponse::error(503, "ledger store unavailable");
  for (const auto& pid : players) cache_->del("balance:" + pid + ":credits", &err);

  JVal v = jobj();
  jset(v, "ok", jbool(true));
  jset(v, "entries", jnum((double)entries.size()));
  return jsonRes(v);
}

JVal Api::chatHistoryJson() {
  JVal arr = jarr();
  std::lock_guard<std::mutex> lock(chatMu_);
  for (const auto& raw : chatLog_) {
    try {
      arr.arr.push_back(parseJson(raw));
    } catch (...) {
    }
  }
  return arr;
}

void Api::addChat(const std::string& messageJson) {
  std::lock_guard<std::mutex> lock(chatMu_);
  chatLog_.push_back(messageJson);
  while (chatLog_.size() > 100) chatLog_.pop_front();
}

void Api::handleUpgrade(Socket& sock, const HttpRequest& req) {
  std::string token = req.param("token");
  if (token.empty()) token = tokenOf(req);

  std::string pid;
  bool found = false;
  std::string err;
  if (token.empty() ||
      !sessions_->resolve(token, &pid, &found, &err) || !found) {
    wsSendFrame(sock.fd(), 0x8, "");
    return;
  }
  sessions_->touch(token, &err);

  // Long-lived: no receive timeout (the server's stop() shuts the fd down).
  sock.setTimeouts(0, 30000);

  auto conn = hub_.add(pid, sock.fd());

  PlayerRecord rec;
  bool recFound = false;
  players_->get(pid, &rec, &recFound, &err);
  int64_t xp = 0;
  int64_t balance = 0;
  totalXpOf(pid, &xp, &err);
  balanceOf(pid, &balance, &err);

  JVal hello = jobj();
  jset(hello, "type", jstr("hello"));
  jset(hello, "playerId", jstr(pid));
  if (recFound) jset(hello, "player", rec.publicJson());
  jset(hello, "progression", progressionView(xp));
  jset(hello, "balance", jnum((double)balance));
  hub_.send(conn, dumpJson(hello));

  // A newcomer immediately sees everyone currently in the world, plus recent
  // chat, so arriving somewhere feels like arriving somewhere populated.
  JVal snapshot = jobj();
  jset(snapshot, "type", jstr("world.snapshot"));
  JVal playersArr = jarr();
  for (const auto& entry : presence_.list())
    playersArr.arr.push_back(entry.toJson());
  jset(snapshot, "players", std::move(playersArr));
  jset(snapshot, "chat", chatHistoryJson());
  hub_.send(conn, dumpJson(snapshot));

  std::string lastMap;
  double lastX = 0, lastY = 0;
  int64_t lastMoveMs = 0;
  int64_t lastChatMs = 0;
  bool announced = false;

  for (;;) {
    int opcode = 0;
    std::string payload;
    int r = wsReadFrame(sock, &opcode, &payload);
    if (r <= 0) break;
    if (opcode == 0x8) break;  // close
    if (opcode == 0x9) {       // ping
      std::lock_guard<std::mutex> lock(conn->writeMu);
      wsSendFrame(conn->fd, 0xA, payload);
      continue;
    }
    if (opcode != 0x1) continue;

    JVal msg;
    try {
      msg = parseJson(payload);
    } catch (...) {
      continue;
    }
    std::string type = msg.s("type");

    if (type == "ping") {
      JVal pong = jobj();
      jset(pong, "type", jstr("pong"));
      jset(pong, "ts", jnum((double)nowMs()));
      hub_.send(conn, dumpJson(pong));
      continue;
    }

    if (type == "world.state") {
      double x = msg.n("x", 0);
      double y = msg.n("y", 0);
      if (!std::isfinite(x) || !std::isfinite(y)) continue;
      if (x < -100000 || x > 100000 || y < -100000 || y > 100000) continue;

      std::string mapId = msg.s("mapId", "town");
      if (mapId.size() > 32) mapId.resize(32);
      bool safeMap = !mapId.empty();
      for (char c : mapId) {
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) {
          safeMap = false;
          break;
        }
      }
      if (!safeMap) mapId = "town";

      PlayerRecord live;
      bool liveFound = false;
      players_->get(pid, &live, &liveFound, &err);

      PresenceEntry entry;
      entry.playerId = pid;
      entry.handle = liveFound ? live.handle : pid;
      entry.displayName = liveFound ? live.displayName : pid;
      entry.appearance = liveFound ? live.appearance : jobj();
      entry.homeUnit = liveFound ? live.homeUnit : 0;
      entry.x = x;
      entry.y = y;
      entry.mapId = mapId;
      presence_.update(entry);

      int64_t now = nowMs();
      double dx = x - lastX, dy = y - lastY;
      bool movedEnough = dx * dx + dy * dy >= 0.5625;  // 0.75 px
      if (!announced || mapId != lastMap || movedEnough ||
          now - lastMoveMs >= 500) {
        JVal out = jobj();
        jset(out, "type", jstr("world.move"));
        jset(out, "player", entry.toJson());
        hub_.broadcast(dumpJson(out));
        lastX = x;
        lastY = y;
        lastMap = mapId;
        lastMoveMs = now;
        announced = true;
      }
      continue;
    }

    if (type == "chat.send") {
      std::string text = trim(msg.s("text"));
      if (text.empty()) continue;
      int64_t now = nowMs();
      if (now - lastChatMs < 700) {
        JVal slow = jobj();
        jset(slow, "type", jstr("chat.error"));
        jset(slow, "message", jstr("you are sending messages too quickly"));
        hub_.send(conn, dumpJson(slow));
        continue;
      }
      if (text.size() > 280) {
        text.resize(280);
        while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80)
          text.pop_back();
      }
      lastChatMs = now;

      std::string zone = "town";
      for (const auto& entry : presence_.list()) {
        if (entry.playerId == pid) {
          zone = entry.mapId;
          break;
        }
      }

      JVal from = jobj();
      jset(from, "id", jstr(pid));
      jset(from, "handle", jstr(recFound ? rec.handle : pid));
      jset(from, "displayName", jstr(recFound ? rec.displayName : pid));
      jset(from, "appearance", recFound ? rec.appearance : jobj());

      JVal out = jobj();
      jset(out, "type", jstr("chat.message"));
      jset(out, "id", jstr(randomBase64Url(6)));
      jset(out, "from", std::move(from));
      jset(out, "text", jstr(text));
      jset(out, "at", jstr(isoNow()));
      jset(out, "zone", jstr(zone));
      std::string raw = dumpJson(out);
      addChat(raw);
      hub_.broadcast(raw);

      JVal props = jobj();
      jset(props, "text", jstr(text));
      jset(props, "zone", jstr(zone));
      history_->insertEvents(
          {{pid, "chat.message", "", "game-server", dumpJson(props)}}, &err);
      continue;
    }

    if (type == "world.leave") break;
  }

  hub_.remove(sock.fd());
  // Only announce departure when the last tab for this account closes.
  if (hub_.countPlayer(pid) == 0) {
    presence_.remove(pid);
    JVal ev = jobj();
    jset(ev, "type", jstr("world.leave"));
    jset(ev, "playerId", jstr(pid));
    hub_.broadcast(dumpJson(ev));
  }
}

HttpResponse Api::handleMarketList(const HttpRequest& req) {
  if (!marketCatalog_) return HttpResponse::error(404, "market not configured");
  std::string regionId = req.param("region");
  if (regionId.empty()) return HttpResponse::error(400, "region is required");
  const MarketRegion* region = marketCatalog_->region(regionId);
  if (!region) return HttpResponse::error(400, "unknown region");
  std::string err;
  JVal v = jobj();
  JVal rj = jobj();
  jset(rj, "id", jstr(region->id));
  jset(rj, "name", jstr(region->name));
  jset(rj, "livingCost", jnum((double)region->livingCost));
  jset(rj, "marketBias", jnum((double)region->marketBias));
  jset(v, "region", rj);
  JVal goods = jarr();
  for (const auto& [id, good] : marketCatalog_->goods()) {
    int64_t demand = 0;
    if (!markets_.demand(id, &demand, &err))
      return HttpResponse::error(503, "market store unavailable");
    JVal g = jobj();
    jset(g, "id", jstr(good.id));
    jset(g, "name", jstr(good.name));
    jset(g, "category", jstr(good.category));
    jset(g, "basePrice", jnum((double)good.basePrice));
    jset(g, "price", jnum((double)MarketCatalog::priceFor(good, *region, demand)));
    jset(g, "demand", jnum((double)demand));
    jset(g, "discontinued", jbool(good.discontinued));
    jset(g, "replacedBy", jstr(good.replacedBy));
    goods.arr.push_back(g);
  }
  jset(v, "goods", goods);
  return jsonRes(v);
}

HttpResponse Api::handleMarketBuy(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  if (!marketCatalog_) return HttpResponse::error(404, "market not configured");
  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  std::string goodId = body.s("goodId");
  std::string regionId = body.s("region");
  int64_t qty = (int64_t)body.n("quantity", 1);
  if (goodId.empty()) return HttpResponse::error(400, "goodId is required");
  if (regionId.empty()) return HttpResponse::error(400, "region is required");
  if (qty < 1 || qty > 100) return HttpResponse::error(400, "quantity must be 1-100");
  const MarketGood* good = marketCatalog_->good(goodId);
  if (!good) return HttpResponse::error(404, "no such good");
  const MarketRegion* region = marketCatalog_->region(regionId);
  if (!region) return HttpResponse::error(400, "unknown region");
  if (good->discontinued) {
    std::string msg = "discontinued";
    if (!good->replacedBy.empty()) msg += ", replaced by " + good->replacedBy;
    return HttpResponse::error(400, msg);
  }

  // One buy is one atomic step per process: price, payment, demand and
  // inventory move under one lock. If the ledger write fails, the demand
  // and inventory changes roll back so nobody is charged without goods and
  // nobody gets goods without paying.
  std::lock_guard<std::mutex> lock(marketMu_);
  std::string err;
  int64_t demand = 0;
  if (!markets_.demand(goodId, &demand, &err))
    return HttpResponse::error(503, "market store unavailable");
  int64_t unit = MarketCatalog::priceFor(*good, *region, demand);
  int64_t total = unit * qty;
  int64_t balance = 0;
  if (!balanceOf(a.playerId, &balance, &err))
    return HttpResponse::error(503, "history store unavailable");
  if (balance < total) return HttpResponse::error(400, "insufficient balance");
  if (!markets_.addDemand(goodId, qty, &err) ||
      !markets_.addInventory(a.playerId, goodId, qty, &err))
    return HttpResponse::error(503, "market store unavailable");

  std::string ref = "market:" + goodId + ":" + a.playerId + ":" + std::to_string(nowMs());
  std::vector<LedgerEntry> entries = {
      {ref, a.playerId, "wallet", -total, "credits", "buy " + goodId},
      {ref, a.playerId, "system", total, "credits", "buy " + goodId},
  };
  if (!validateTransaction(entries, &err)) {
    markets_.addDemand(goodId, -qty, &err);
    markets_.addInventory(a.playerId, goodId, -qty, &err);
    return HttpResponse::error(400, err);
  }
  std::vector<LedgerRow> rows;
  for (const auto& e : entries)
    rows.push_back({e.ref, e.playerId, e.account, e.amount, e.currency, e.memo});
  if (!history_->insertLedger(rows, &err)) {
    markets_.addDemand(goodId, -qty, &err);
    markets_.addInventory(a.playerId, goodId, -qty, &err);
    return HttpResponse::error(503, "ledger store unavailable");
  }
  cache_->del("balance:" + a.playerId + ":credits", &err);

  PlayerRecord rec;
  bool found = false;
  std::string handle = a.playerId;
  if (players_->get(a.playerId, &rec, &found, &err) && found) handle = rec.handle;
  WorldEvent ev;
  ev.type = "market.purchase";
  ev.title = handle + " bought " + good->name;
  ev.actorId = a.playerId;
  ev.city = regionId;
  events_.record(ev, nullptr, &err);

  int64_t newBalance = 0;
  if (!balanceOf(a.playerId, &newBalance, &err))
    return HttpResponse::error(503, "history store unavailable");
  JVal v = jobj();
  jset(v, "goodId", jstr(goodId));
  jset(v, "quantity", jnum((double)qty));
  jset(v, "unitPrice", jnum((double)unit));
  jset(v, "total", jnum((double)total));
  jset(v, "balance", jnum((double)newBalance));
  return jsonRes(v);
}

HttpResponse Api::handleMarketInventory(const HttpRequest& req) {
  Auth a = requirePlayer(req);
  if (!a.ok) return a.response;
  if (!marketCatalog_) return HttpResponse::error(404, "market not configured");
  std::string err;
  std::vector<InventoryItem> items;
  if (!markets_.inventory(a.playerId, &items, &err))
    return HttpResponse::error(503, "market store unavailable");
  JVal v = jobj();
  JVal arr = jarr();
  for (const auto& it : items) {
    const MarketGood* good = marketCatalog_->good(it.goodId);
    if (!good) continue;
    JVal o = jobj();
    jset(o, "goodId", jstr(it.goodId));
    jset(o, "name", jstr(good->name));
    jset(o, "qty", jnum((double)it.qty));
    arr.arr.push_back(o);
  }
  jset(v, "items", arr);
  return jsonRes(v);
}

HttpResponse Api::handleMarketRestock(const HttpRequest& req) {
  if (cfg_.adminKey.empty()) return HttpResponse::error(404, "not found");
  if (req.header("x-admin-key") != cfg_.adminKey)
    return HttpResponse::error(401, "unauthorized");
  if (!marketCatalog_) return HttpResponse::error(404, "market not configured");
  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;
  std::string goodId = body.s("goodId");
  int64_t supply = (int64_t)body.n("supply", 0);
  if (goodId.empty()) return HttpResponse::error(400, "goodId is required");
  if (supply < 1) return HttpResponse::error(400, "supply must be at least 1");
  const MarketGood* good = marketCatalog_->good(goodId);
  if (!good) return HttpResponse::error(404, "no such good");
  std::string err;
  {
    std::lock_guard<std::mutex> lock(marketMu_);
    if (!markets_.addDemand(goodId, -supply, &err))
      return HttpResponse::error(503, "market store unavailable");
  }
  WorldEvent ev;
  ev.type = "market.restocked";
  ev.title = "New supply of " + good->name;
  events_.record(ev, nullptr, &err);
  int64_t demand = 0;
  if (!markets_.demand(goodId, &demand, &err))
    return HttpResponse::error(503, "market store unavailable");
  JVal v = jobj();
  jset(v, "goodId", jstr(goodId));
  jset(v, "demand", jnum((double)demand));
  return jsonRes(v);
}

}  // namespace dw
