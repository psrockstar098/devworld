#pragma once
// players.h — durable identity records in Kvrocks, including the password hash.
// The public JSON never includes the hash or internal flags.
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

struct PlayerRecord {
  std::string id;
  std::string username;  // login name, lower-cased for uniqueness
  std::string handle;    // public, 3-24 chars
  std::string displayName;
  std::string passwordHash;
  std::string createdAt;
  std::string updatedAt;
  std::vector<std::string> completedMissions;
  JVal flags = jobj();
  JVal appearance = jobj();  // {skin:[r,g,b],shirt:[...],pants:[...],hair:[...]}
  int homeUnit = 0;          // assigned apartment in the shared Home Row

  std::string toJson() const;        // storage (includes the hash)
  JVal publicJson() const;           // API (never includes the hash)
  static bool fromJson(const JVal& j, PlayerRecord* out);
};

class PlayerStore {
 public:
  explicit PlayerStore(KvStore* kv) : kv_(kv) {}

  // Returns false on store failure; `status` is an HTTP-ish code for validation
  // conflicts (400/409) and 500 for I/O.
  bool create(const std::string& username, const std::string& password,
              const std::string& displayName, PlayerRecord* out, int* status,
              std::string* err);
  bool get(const std::string& id, PlayerRecord* out, bool* found,
           std::string* err);
  bool getByUsername(const std::string& username, PlayerRecord* out, bool* found,
                     std::string* err);
  bool getByHandle(const std::string& handle, PlayerRecord* out, bool* found,
                   std::string* err);
  bool save(const PlayerRecord& rec, std::string* err);
  bool markCompleted(const std::string& id, const std::string& missionId,
                     std::string* err);

  static bool validUsername(const std::string& s);
  static bool validHandle(const std::string& s);
  static bool validId(const std::string& s);
  static std::string newId();
  static std::string playerKey(const std::string& id) { return "player:" + id; }
  static std::string usernameKey(const std::string& username) {
    return "username:" + toLowerCopy(username);
  }
  static std::string handleKey(const std::string& handle) {
    return "handle:" + handle;
  }

 private:
  static std::string toLowerCopy(const std::string& s);
  KvStore* kv_;
};

}  // namespace dw
