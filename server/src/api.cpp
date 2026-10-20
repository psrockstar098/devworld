#include "api.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#include "crypto.h"
#include "json.h"
#include "ledger.h"
#include "progression.h"
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

}  // namespace

Api::Api(History* history, KvStore* kv, KvStore* cache, PlayerStore* players,
         SessionStore* sessions, MissionCatalog* missions, AssessClient* assess,
         ApiConfig cfg)
    : history_(history), kv_(kv), cache_(cache), players_(players),
      sessions_(sessions), missions_(missions), assess_(assess),
      cfg_(std::move(cfg)), friends_(kv), presence_(cache) {}

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
    if ((path == "/me" || path == "/progression/me") && method == "GET")
      return handleMe(req);
    if (path == "/missions" && method == "GET") return handleMissions();
    if (path == "/stats/missions" && method == "GET") return handleMissionStats();
    if (path == "/ledger/balance" && method == "GET") return handleBalance(req);
    if (path == "/admin/ledger" && method == "POST") return handleAdminLedger(req);
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

HttpResponse Api::handleRegister(const HttpRequest& req) {
  JVal body;
  HttpResponse bad;
  if (!readObject(req, &body, &bad)) return bad;

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

  std::string username = body.s("username");
  std::string password = body.s("password");
  std::string legacyId = body.s("playerId");

  std::string err;
  PlayerRecord rec;
  bool found = false;

  if (!username.empty() || !password.empty()) {
    if (username.empty() || password.empty())
      return HttpResponse::error(400, "username and password are required");
    if (!players_->getByUsername(username, &rec, &found, &err))
      return HttpResponse::error(500, "player store unavailable");
    // Always run the KDF so a missing username is not a timing oracle.
    const std::string& hash = found ? rec.passwordHash : dummyHash();
    bool valid = passwordVerify(password, hash);
    if (!found || !valid)
      return HttpResponse::error(401, "invalid credentials");
  } else if (!legacyId.empty()) {
    if (!players_->get(legacyId, &rec, &found, &err))
      return HttpResponse::error(500, "player store unavailable");
    if (!found) return HttpResponse::error(404, "no such player");
  } else {
    return HttpResponse::error(400, "username and password are required");
  }

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

  JVal v = rec.publicJson();
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

HttpResponse Api::handleMissions() { return jsonRes(missions_->listJson()); }

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

}  // namespace dw
