#include "events.h"

#include <algorithm>

#include "util.h"

namespace dw {
namespace {

// Same id shape as the reference server: base-36 time plus a random tail.
std::string base36(uint64_t n) {
  static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (n == 0) return "0";
  std::string out;
  while (n) {
    out.insert(out.begin(), kDigits[n % 36]);
    n /= 36;
  }
  return out;
}

std::string newId() {
  return base36(static_cast<uint64_t>(nowMs())) + "-" + randomHex(5);
}

}  // namespace

JVal WorldEvent::toJson() const {
  JVal j = jobj();
  jset(j, "id", jstr(id));
  jset(j, "type", jstr(type));
  jset(j, "title", jstr(title));
  jset(j, "detail", jstr(detail));
  jset(j, "actorId", actorId.empty() ? jnull() : jstr(actorId));
  jset(j, "city", city.empty() ? jnull() : jstr(city));
  jset(j, "createdAtMs", jnum(static_cast<double>(createdAtMs)));
  return j;
}

bool validEventType(const std::string& type) {
  if (type.empty() || type.size() > 64) return false;
  for (unsigned char c : type) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool EventStore::readAll(std::vector<WorldEvent>* out, std::string* err) {
  std::string raw;
  bool found = false;
  if (!kv_->get(key(), &raw, &found, err)) return false;
  out->clear();
  if (!found) return true;
  try {
    JVal doc = parseJson(raw);
    if (doc.type != JVal::ARR) return true;  // corrupt log: start fresh
    for (const JVal& j : doc.arr) {
      if (j.type != JVal::OBJ) continue;
      WorldEvent ev;
      ev.id = j.s("id");
      ev.type = j.s("type");
      ev.title = j.s("title");
      ev.detail = j.s("detail");
      if (j.at("actorId").type == JVal::STR) ev.actorId = j.at("actorId").str;
      if (j.at("city").type == JVal::STR) ev.city = j.at("city").str;
      ev.createdAtMs = static_cast<int64_t>(j.n("createdAtMs"));
      out->push_back(std::move(ev));
    }
  } catch (const std::exception&) {
    // Corrupt log: start fresh rather than serving bad state.
    out->clear();
  }
  return true;
}

bool EventStore::save(const std::vector<WorldEvent>& events, std::string* err) {
  JVal doc = jarr();
  for (const WorldEvent& e : events) doc.arr.push_back(e.toJson());
  return kv_->set(key(), dumpJson(doc), 0, err);
}

bool EventStore::record(const WorldEvent& in, WorldEvent* out,
                        std::string* err) {
  std::vector<WorldEvent> all;
  if (!readAll(&all, err)) return false;
  WorldEvent ev = in;
  ev.id = newId();
  ev.createdAtMs = nowMs();
  all.push_back(ev);
  if (all.size() > static_cast<size_t>(kMaxEvents))
    all.erase(all.begin(), all.end() - kMaxEvents);
  if (!save(all, err)) return false;
  if (out) *out = std::move(ev);
  return true;
}

bool EventStore::list(const std::string& type, int limit,
                      std::vector<WorldEvent>* out, std::string* err) {
  out->clear();
  if (limit <= 0) limit = kDefaultList;
  if (limit > kMaxList) limit = kMaxList;
  std::vector<WorldEvent> all;
  if (!readAll(&all, err)) return false;
  for (auto it = all.rbegin(); it != all.rend() &&
                              out->size() < static_cast<size_t>(limit);
       ++it) {
    if (!type.empty() && it->type != type) continue;
    out->push_back(*it);
  }
  return true;
}

}  // namespace dw
