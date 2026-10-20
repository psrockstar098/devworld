#include "players.h"

#include <cctype>

#include "crypto.h"
#include "util.h"

namespace dw {
namespace {

// The same palette the character creator starts with. Kept here so an account
// created through the API (not the game) still has a visible person.
JVal defaultAppearance() {
  auto rgb = [](int r, int g, int b) {
    JVal a = jarr();
    a.arr.push_back(jnum(r));
    a.arr.push_back(jnum(g));
    a.arr.push_back(jnum(b));
    return a;
  };
  JVal j = jobj();
  jset(j, "skin", rgb(232, 190, 150));
  jset(j, "shirt", rgb(45, 212, 191));
  jset(j, "pants", rgb(51, 65, 85));
  jset(j, "hair", rgb(45, 35, 28));
  return j;
}

JVal appearanceOrDefault(const JVal& v) {
  return v.type == JVal::OBJ ? v : defaultAppearance();
}

}  // namespace

std::string PlayerStore::toLowerCopy(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = (char)std::tolower((unsigned char)c);
  return o;
}

bool PlayerStore::validUsername(const std::string& s) {
  if (s.size() < 3 || s.size() > 32) return false;
  for (char c : s) {
    if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
  }
  return true;
}

bool PlayerStore::validHandle(const std::string& s) {
  if (s.size() < 3 || s.size() > 24) return false;
  for (char c : s) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
      return false;
  }
  return true;
}

bool PlayerStore::validId(const std::string& s) {
  if (s.size() < 6 || s.size() > 64) return false;
  for (char c : s) {
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

std::string PlayerStore::newId() { return randomBase64Url(12); }

std::string PlayerRecord::toJson() const {
  JVal j = jobj();
  jset(j, "id", jstr(id));
  jset(j, "username", jstr(username));
  jset(j, "handle", jstr(handle));
  jset(j, "displayName", jstr(displayName));
  jset(j, "passwordHash", jstr(passwordHash));
  jset(j, "createdAt", jstr(createdAt));
  jset(j, "updatedAt", jstr(updatedAt));
  JVal missions = jarr();
  for (const auto& m : completedMissions) missions.arr.push_back(jstr(m));
  jset(j, "completedMissions", missions);
  jset(j, "flags", flags.type == JVal::OBJ ? flags : jobj());
  jset(j, "appearance", appearanceOrDefault(appearance));
  jset(j, "homeUnit", jnum(homeUnit));
  return dumpJson(j);
}

JVal PlayerRecord::publicJson() const {
  JVal j = jobj();
  jset(j, "id", jstr(id));
  jset(j, "username", jstr(username));
  jset(j, "handle", jstr(handle));
  jset(j, "displayName", jstr(displayName));
  jset(j, "createdAt", jstr(createdAt));
  JVal missions = jarr();
  for (const auto& m : completedMissions) missions.arr.push_back(jstr(m));
  jset(j, "completedMissions", missions);
  jset(j, "appearance", appearanceOrDefault(appearance));
  jset(j, "homeUnit", jnum(homeUnit));
  return j;
}

bool PlayerRecord::fromJson(const JVal& j, PlayerRecord* out) {
  if (j.type != JVal::OBJ) return false;
  out->id = j.s("id");
  out->username = j.s("username");
  out->handle = j.s("handle");
  out->displayName = j.s("displayName");
  out->passwordHash = j.s("passwordHash");
  out->createdAt = j.s("createdAt");
  out->updatedAt = j.s("updatedAt");
  out->completedMissions.clear();
  const JVal& ms = j.at("completedMissions");
  if (ms.type == JVal::ARR) {
    for (const auto& m : ms.arr)
      if (m.type == JVal::STR) out->completedMissions.push_back(m.str);
  }
  const JVal& f = j.at("flags");
  out->flags = f.type == JVal::OBJ ? f : jobj();
  out->appearance = appearanceOrDefault(j.at("appearance"));
  out->homeUnit = j.i("homeUnit", 0);
  return !out->id.empty();
}

bool PlayerStore::create(const std::string& username,
                         const std::string& password,
                         const std::string& displayName, PlayerRecord* out,
                         int* status, std::string* err) {
  auto fail = [&](int code, const std::string& msg) {
    if (status) *status = code;
    if (err) *err = msg;
    return false;
  };
  if (!validUsername(username))
    return fail(400, "username must be 3-32 chars of letters, digits or _");
  if (password.size() < 8 || password.size() > 200)
    return fail(400, "password must be 8-200 characters");

  std::string uname = toLowerCopy(username);
  std::string existing;
  bool found = false;
  if (!kv_->get(usernameKey(uname), &existing, &found, err)) {
    if (status) *status = 500;
    return false;
  }
  if (found) return fail(409, "username already taken");

  std::string id = newId();
  std::string handle = uname;
  if (!validHandle(handle)) handle.clear();

  auto handleTaken = [&](const std::string& h, bool* taken) {
    *taken = false;
    std::string v;
    return kv_->get(handleKey(h), &v, taken, err);
  };

  if (!handle.empty()) {
    bool taken = false;
    if (!handleTaken(handle, &taken)) {
      if (status) *status = 500;
      return false;
    }
    if (taken) handle.clear();
  }
  for (int i = 0; handle.empty() && i < 8; ++i) {
    std::string candidate = "player_" + randomHex(4);
    bool taken = false;
    if (!handleTaken(candidate, &taken)) {
      if (status) *status = 500;
      return false;
    }
    if (!taken) handle = candidate;
  }
  if (handle.empty()) return fail(409, "could not allocate a handle");

  PlayerRecord rec;
  rec.id = id;
  rec.username = uname;
  rec.handle = handle;
  rec.displayName = displayName.empty() ? handle : displayName.substr(0, 32);
  rec.passwordHash = passwordHash(password);
  rec.createdAt = isoNow();
  rec.updatedAt = rec.createdAt;
  rec.flags = jobj();
  rec.appearance = defaultAppearance();
  if (rec.passwordHash.empty())
    return fail(500, "could not hash password");

  if (!kv_->set(playerKey(rec.id), rec.toJson(), 0, err) ||
      !kv_->set(usernameKey(uname), rec.id, 0, err) ||
      !kv_->set(handleKey(handle), rec.id, 0, err)) {
    if (status) *status = 500;
    return false;
  }
  if (out) *out = rec;
  return true;
}

bool PlayerStore::get(const std::string& id, PlayerRecord* out, bool* found,
                      std::string* err) {
  *found = false;
  if (!validId(id)) return true;
  std::string raw;
  if (!kv_->get(playerKey(id), &raw, found, err)) return false;
  if (!*found) return true;
  JVal j;
  try {
    j = parseJson(raw);
  } catch (...) {
    if (err) *err = "corrupt player record";
    return false;
  }
  return PlayerRecord::fromJson(j, out);
}

bool PlayerStore::getByUsername(const std::string& username, PlayerRecord* out,
                                bool* found, std::string* err) {
  *found = false;
  std::string id;
  bool idFound = false;
  if (!kv_->get(usernameKey(username), &id, &idFound, err)) return false;
  if (!idFound) return true;
  return get(id, out, found, err);
}

bool PlayerStore::getByHandle(const std::string& handle, PlayerRecord* out,
                              bool* found, std::string* err) {
  *found = false;
  if (!validHandle(handle)) return true;
  std::string id;
  bool idFound = false;
  if (!kv_->get(handleKey(handle), &id, &idFound, err)) return false;
  if (!idFound) return true;
  return get(id, out, found, err);
}

bool PlayerStore::save(const PlayerRecord& rec, std::string* err) {
  PlayerRecord copy = rec;
  copy.updatedAt = isoNow();
  return kv_->set(playerKey(copy.id), copy.toJson(), 0, err);
}

bool PlayerStore::markCompleted(const std::string& id,
                                const std::string& missionId,
                                std::string* err) {
  PlayerRecord rec;
  bool found = false;
  if (!get(id, &rec, &found, err)) return false;
  if (!found) {
    if (err) *err = "no such player";
    return false;
  }
  for (const auto& m : rec.completedMissions)
    if (m == missionId) return true;
  rec.completedMissions.push_back(missionId);
  return save(rec, err);
}

}  // namespace dw
