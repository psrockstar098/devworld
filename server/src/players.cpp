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

JVal identityJson(const OAuthIdentity& id) {
  JVal j = jobj();
  jset(j, "provider", jstr(id.provider));
  jset(j, "subject", jstr(id.subject));
  jset(j, "email", jstr(id.email));
  jset(j, "emailVerified", jbool(id.emailVerified));
  jset(j, "login", jstr(id.login));
  jset(j, "displayName", jstr(id.displayName));
  jset(j, "avatarUrl", jstr(id.avatarUrl));
  jset(j, "linkedAt", jstr(isoNow()));
  return j;
}

const JVal* findIdentity(const PlayerRecord& rec, const std::string& provider) {
  if (rec.identities.type != JVal::ARR) return nullptr;
  for (const auto& i : rec.identities.arr) {
    if (i.type == JVal::OBJ && i.s("provider") == provider) return &i;
  }
  return nullptr;
}

std::string slugify(std::string s) {
  s = toLower(std::move(s));
  std::string out;
  bool lastUnderscore = false;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += (char)c;
      lastUnderscore = false;
    } else if (!lastUnderscore && !out.empty()) {
      out += '_';
      lastUnderscore = true;
    }
    if (out.size() >= 24) break;
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.size() < 3) {
    if (!out.empty()) out += '_';
    out += randomHex(2);
  }
  if (out.size() > 24) out.resize(24);
  return out;
}

std::string emailLocalPart(const std::string& email) {
  size_t at = email.find('@');
  return at == std::string::npos ? email : email.substr(0, at);
}

std::string cleanDisplayName(const std::string& in) {
  std::string out;
  for (unsigned char c : in) {
    if (c >= 0x20 && c != 0x7f) out += (char)c;
    if (out.size() >= 32) break;
  }
  return trim(out);
}

}  // namespace

std::string PlayerStore::toLowerCopy(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = (char)std::tolower((unsigned char)c);
  return o;
}

bool PlayerStore::validProvider(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-'))
      return false;
  }
  return true;
}

std::string PlayerStore::normalizeEmail(const std::string& s) {
  std::string e = toLowerCopy(trim(s));
  if (e.size() < 3 || e.size() > 254) return "";
  size_t at = e.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= e.size()) return "";
  if (e.find('@', at + 1) != std::string::npos) return "";
  for (unsigned char c : e)
    if (std::isspace(c) || c < 0x20) return "";
  return e;
}

std::string PlayerStore::emailKey(const std::string& email) {
  return "email:" + normalizeEmail(email);
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
  jset(j, "email", jstr(email));
  jset(j, "emailVerified", jbool(emailVerified));
  jset(j, "createdAt", jstr(createdAt));
  jset(j, "updatedAt", jstr(updatedAt));
  JVal missions = jarr();
  for (const auto& m : completedMissions) missions.arr.push_back(jstr(m));
  jset(j, "completedMissions", missions);
  jset(j, "flags", flags.type == JVal::OBJ ? flags : jobj());
  jset(j, "appearance", appearanceOrDefault(appearance));
  jset(j, "identities",
       identities.type == JVal::ARR ? identities : jarr());
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

JVal PlayerRecord::selfJson() const {
  JVal j = publicJson();
  jset(j, "email", jstr(email));
  jset(j, "emailVerified", jbool(emailVerified));
  JVal providers = jarr();
  JVal auth = jarr();
  if (identities.type == JVal::ARR) {
    for (const auto& i : identities.arr) {
      if (i.type != JVal::OBJ) continue;
      std::string provider = i.s("provider");
      if (provider.empty()) continue;
      providers.arr.push_back(jstr(provider));
      JVal one = jobj();
      jset(one, "provider", jstr(provider));
      jset(one, "login", jstr(i.s("login")));
      jset(one, "displayName", jstr(i.s("displayName")));
      jset(one, "avatarUrl", jstr(i.s("avatarUrl")));
      jset(one, "linkedAt", jstr(i.s("linkedAt")));
      auth.arr.push_back(one);
    }
  }
  jset(j, "providers", providers);
  jset(j, "identities", auth);
  return j;
}

bool PlayerRecord::fromJson(const JVal& j, PlayerRecord* out) {
  if (j.type != JVal::OBJ) return false;
  out->id = j.s("id");
  out->username = j.s("username");
  out->handle = j.s("handle");
  out->displayName = j.s("displayName");
  out->passwordHash = j.s("passwordHash");
  out->email = j.s("email");
  out->emailVerified = j.boolean("emailVerified");
  out->createdAt = j.s("createdAt");
  out->updatedAt = j.s("updatedAt");
  out->identities = j.at("identities");
  if (out->identities.type != JVal::ARR) out->identities = jarr();
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

bool PlayerStore::createOAuth(const OAuthIdentity& identity, PlayerRecord* out,
                              int* status, std::string* err) {
  auto fail = [&](int code, const std::string& msg) {
    if (status) *status = code;
    if (err) *err = msg;
    return false;
  };
  if (!validProvider(identity.provider))
    return fail(400, "invalid identity provider");
  if (identity.subject.empty() || identity.subject.size() > 256)
    return fail(400, "invalid identity subject");

  std::string existing;
  bool found = false;
  if (!kv_->get(oauthKey(identity.provider, identity.subject), &existing, &found,
                err)) {
    if (status) *status = 500;
    return false;
  }
  if (found) return fail(409, "identity already linked");

  std::string seed = identity.login;
  if (seed.empty()) seed = emailLocalPart(identity.email);
  if (seed.empty()) seed = identity.displayName;
  std::string base = slugify(seed);

  std::string uname, handle;
  for (int attempt = 0; attempt < 12 && handle.empty(); ++attempt) {
    std::string candidate = base;
    if (attempt > 0) {
      std::string suffix = "_" + randomHex(2);
      size_t keep = 24 > suffix.size() ? 24 - suffix.size() : 1;
      candidate = base.substr(0, keep) + suffix;
    }
    std::string value;
    bool usernameTaken = false;
    bool handleTaken = false;
    if (!kv_->get(usernameKey(candidate), &value, &usernameTaken, err) ||
        !kv_->get(handleKey(candidate), &value, &handleTaken, err)) {
      if (status) *status = 500;
      return false;
    }
    if (!usernameTaken && !handleTaken) {
      uname = candidate;
      handle = candidate;
    }
  }
  if (handle.empty()) {
    uname = "dev_" + randomHex(6);
    handle = uname;
  }

  PlayerRecord rec;
  rec.id = newId();
  rec.username = uname;
  rec.handle = handle;
  rec.displayName = cleanDisplayName(identity.displayName);
  if (rec.displayName.empty()) rec.displayName = identity.login;
  if (rec.displayName.empty()) rec.displayName = handle;
  rec.email = normalizeEmail(identity.email);
  rec.emailVerified = identity.emailVerified && !rec.email.empty();
  rec.passwordHash.clear();
  rec.createdAt = isoNow();
  rec.updatedAt = rec.createdAt;
  rec.flags = jobj();
  jset(rec.flags, "oauth", jbool(true));
  jset(rec.flags, "oauthProvider", jstr(identity.provider));
  rec.appearance = defaultAppearance();
  rec.identities = jarr();
  rec.identities.arr.push_back(identityJson(identity));

  // Only a provider-verified email may claim the email index; that index is
  // what links a second provider to this account.
  if (rec.emailVerified && !rec.email.empty()) {
    std::string owner;
    bool emailTaken = false;
    if (!kv_->get(emailKey(rec.email), &owner, &emailTaken, err)) {
      if (status) *status = 500;
      return false;
    }
    if (emailTaken) return fail(409, "this email is already linked to another account");
  }

  if (!kv_->set(playerKey(rec.id), rec.toJson(), 0, err) ||
      !kv_->set(usernameKey(rec.username), rec.id, 0, err) ||
      !kv_->set(handleKey(rec.handle), rec.id, 0, err) ||
      !kv_->set(oauthKey(identity.provider, identity.subject), rec.id, 0, err)) {
    if (status) *status = 500;
    return false;
  }
  if (rec.emailVerified && !rec.email.empty() &&
      !kv_->set(emailKey(rec.email), rec.id, 0, err)) {
    if (status) *status = 500;
    return false;
  }
  if (out) *out = rec;
  return true;
}

bool PlayerStore::linkOAuth(PlayerRecord* rec, const OAuthIdentity& identity,
                            int* status, std::string* err) {
  auto fail = [&](int code, const std::string& msg) {
    if (status) *status = code;
    if (err) *err = msg;
    return false;
  };
  if (!rec) return fail(400, "missing player");
  if (!validProvider(identity.provider) || identity.subject.empty() ||
      identity.subject.size() > 256)
    return fail(400, "invalid identity");

  if (rec->identities.type != JVal::ARR) rec->identities = jarr();

  const JVal* existing = findIdentity(*rec, identity.provider);
  if (existing) {
    if (existing->s("subject") != identity.subject)
      return fail(409, "this account is linked to a different " +
                           identity.provider + " identity");
    // Refresh the profile fields the provider may have changed.
    for (auto& one : rec->identities.arr) {
      if (one.type != JVal::OBJ || one.s("provider") != identity.provider)
        continue;
      if (!identity.login.empty()) jset(one, "login", jstr(identity.login));
      if (!identity.displayName.empty())
        jset(one, "displayName", jstr(identity.displayName));
      if (!identity.avatarUrl.empty())
        jset(one, "avatarUrl", jstr(identity.avatarUrl));
      if (!identity.email.empty()) jset(one, "email", jstr(identity.email));
      jset(one, "emailVerified", jbool(identity.emailVerified));
    }
  } else {
    std::string owner;
    bool linked = false;
    if (!kv_->get(oauthKey(identity.provider, identity.subject), &owner, &linked,
                  err)) {
      if (status) *status = 500;
      return false;
    }
    if (linked && owner != rec->id)
      return fail(409, "this " + identity.provider + " identity is already linked");
    rec->identities.arr.push_back(identityJson(identity));
    if (!kv_->set(oauthKey(identity.provider, identity.subject), rec->id, 0, err)) {
      if (status) *status = 500;
      return false;
    }
  }

  std::string email = normalizeEmail(identity.email);
  bool claimEmail = false;
  if (rec->email.empty() && !email.empty()) {
    rec->email = email;
    rec->emailVerified = identity.emailVerified;
    claimEmail = identity.emailVerified;
  } else if (rec->email == email && identity.emailVerified &&
             !rec->emailVerified) {
    rec->emailVerified = true;
    claimEmail = true;
  }
  if (claimEmail) {
    std::string owner;
    bool taken = false;
    if (!kv_->get(emailKey(email), &owner, &taken, err)) {
      if (status) *status = 500;
      return false;
    }
    if (taken && owner != rec->id)
      return fail(409, "this email is already linked to another account");
    if (!taken && !kv_->set(emailKey(email), rec->id, 0, err)) {
      if (status) *status = 500;
      return false;
    }
  }
  if (!save(*rec, err)) {
    if (status) *status = 500;
    return false;
  }
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

bool PlayerStore::getByOAuth(const std::string& provider,
                             const std::string& subject, PlayerRecord* out,
                             bool* found, std::string* err) {
  *found = false;
  if (!validProvider(provider) || subject.empty() || subject.size() > 256)
    return true;
  std::string id;
  bool idFound = false;
  if (!kv_->get(oauthKey(provider, subject), &id, &idFound, err)) return false;
  if (!idFound) return true;
  return get(id, out, found, err);
}

bool PlayerStore::getByEmail(const std::string& email, PlayerRecord* out,
                             bool* found, std::string* err) {
  *found = false;
  std::string normalized = normalizeEmail(email);
  if (normalized.empty()) return true;
  std::string id;
  bool idFound = false;
  if (!kv_->get(emailKey(normalized), &id, &idFound, err)) return false;
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
