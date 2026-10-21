#pragma once
// events.h — the world event log.
//
// Every consequential thing that happens in the world gets one entry here:
// a company founded, a product launched, a hackathon won, a data center
// outage — and, since play writes its own history, a player joining or a
// mission completed. This log is the single source of truth for the in-game
// newspaper, the world timeline and event-driven NPC dialogue — all three
// read it, none of them invent their own news.
//
// Events are durable (the kv store, not the cache): history must survive
// restarts. Writes are admin-key only; the server's own systems record
// through the same store as they land. Reads are authenticated but open to
// any player: history is public to the world's residents.
#include <cstdint>
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

struct WorldEvent {
  std::string id;
  std::string type;
  std::string title;
  std::string detail;
  std::string actorId;  // empty = nobody in particular
  std::string city;     // empty = world-wide
  int64_t createdAtMs = 0;

  // actorId/city become JSON null when empty.
  JVal toJson() const;
};

class EventStore {
 public:
  explicit EventStore(KvStore* kv) : kv_(kv) {}

  bool record(const WorldEvent& in, WorldEvent* out, std::string* err);
  bool list(const std::string& type, int limit, std::vector<WorldEvent>* out,
            std::string* err);

  static std::string key() { return "events:log"; }
  static constexpr int kMaxEvents = 1000;
  static constexpr int kMaxList = 100;
  static constexpr int kDefaultList = 50;

 private:
  bool readAll(std::vector<WorldEvent>* out, std::string* err);
  bool save(const std::vector<WorldEvent>& events, std::string* err);

  KvStore* kv_;
};

// Event types are short dotted identifiers: [A-Za-z0-9_.-], 1-64 chars.
bool validEventType(const std::string& type);

}  // namespace dw
