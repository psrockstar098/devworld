#include "friends.h"

#include <algorithm>

namespace dw {
namespace {

JVal stringArray(const std::vector<std::string>& v) {
  JVal a = jarr();
  for (const auto& s : v) a.arr.push_back(jstr(s));
  return a;
}

std::vector<std::string> readArray(const JVal& j) {
  std::vector<std::string> out;
  if (j.type != JVal::ARR) return out;
  for (const auto& v : j.arr)
    if (v.type == JVal::STR) out.push_back(v.str);
  return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

void addUnique(std::vector<std::string>& v, const std::string& s) {
  if (!contains(v, s)) v.push_back(s);
}

void eraseValue(std::vector<std::string>& v, const std::string& s) {
  v.erase(std::remove(v.begin(), v.end(), s), v.end());
}

}  // namespace

JVal FriendLinks::toJson() const {
  JVal j = jobj();
  jset(j, "friends", stringArray(friends));
  jset(j, "incoming", stringArray(incoming));
  jset(j, "outgoing", stringArray(outgoing));
  return j;
}

FriendLinks FriendLinks::fromJson(const JVal& j) {
  FriendLinks out;
  out.friends = readArray(j.at("friends"));
  out.incoming = readArray(j.at("incoming"));
  out.outgoing = readArray(j.at("outgoing"));
  return out;
}

bool FriendStore::get(const std::string& playerId, FriendLinks* out,
                      std::string* err) {
  std::string raw;
  bool found = false;
  if (!kv_->get(key(playerId), &raw, &found, err)) return false;
  if (!found) {
    *out = FriendLinks{};
    return true;
  }
  try {
    *out = FriendLinks::fromJson(parseJson(raw));
  } catch (const std::exception& e) {
    if (err) *err = std::string("bad friend record: ") + e.what();
    return false;
  }
  return true;
}

bool FriendStore::save(const std::string& playerId, const FriendLinks& links,
                       std::string* err) {
  return kv_->set(key(playerId), dumpJson(links.toJson()), 0, err);
}

bool FriendStore::request(const std::string& from, const std::string& to,
                          int* status, std::string* err) {
  if (from == to) {
    if (status) *status = 400;
    if (err) *err = "cannot befriend yourself";
    return true;
  }
  FriendLinks a, b;
  if (!get(from, &a, err) || !get(to, &b, err)) return false;
  if (contains(a.friends, to)) {
    if (status) *status = 409;
    if (err) *err = "already friends";
    return true;
  }
  if (contains(b.incoming, from)) {
    if (status) *status = 409;
    if (err) *err = "request already pending";
    return true;
  }
  addUnique(a.outgoing, to);
  addUnique(b.incoming, from);
  if (!save(from, a, err) || !save(to, b, err)) return false;
  if (status) *status = 200;
  return true;
}

bool FriendStore::accept(const std::string& playerId,
                         const std::string& requester, int* status,
                         std::string* err) {
  FriendLinks me, them;
  if (!get(playerId, &me, err) || !get(requester, &them, err)) return false;
  if (!contains(me.incoming, requester)) {
    if (status) *status = 409;
    if (err) *err = "no pending request";
    return true;
  }
  eraseValue(me.incoming, requester);
  eraseValue(them.outgoing, playerId);
  addUnique(me.friends, requester);
  addUnique(them.friends, playerId);
  if (!save(playerId, me, err) || !save(requester, them, err)) return false;
  if (status) *status = 200;
  return true;
}

bool FriendStore::decline(const std::string& playerId,
                          const std::string& requester, int* status,
                          std::string* err) {
  FriendLinks me, them;
  if (!get(playerId, &me, err) || !get(requester, &them, err)) return false;
  eraseValue(me.incoming, requester);
  eraseValue(them.outgoing, playerId);
  if (!save(playerId, me, err) || !save(requester, them, err)) return false;
  if (status) *status = 200;
  return true;
}

bool FriendStore::remove(const std::string& a, const std::string& b,
                         int* status, std::string* err) {
  FriendLinks x, y;
  if (!get(a, &x, err) || !get(b, &y, err)) return false;
  bool existed = contains(x.friends, b) || contains(x.outgoing, b) ||
                 contains(x.incoming, b) || contains(y.friends, a);
  if (!existed) {
    if (status) *status = 404;
    if (err) *err = "not friends";
    return true;  // store read fine; there was simply nothing to remove
  }
  eraseValue(x.friends, b);
  eraseValue(y.friends, a);
  eraseValue(x.outgoing, b);
  eraseValue(y.outgoing, a);
  eraseValue(x.incoming, b);
  eraseValue(y.incoming, a);
  if (!save(a, x, err) || !save(b, y, err)) return false;
  if (status) *status = 200;
  return true;
}

}  // namespace dw
