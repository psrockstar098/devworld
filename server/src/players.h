#pragma once
// players.h — durable identity records in Kvrocks, including the password hash.
// The public JSON never includes the hash or internal flags.
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

// One external identity (Google, GitHub, …) attached to a player. `subject` is
// the provider's stable user id and is never exposed by the API.
struct OAuthIdentity {
  std::string provider;
  std::string subject;
  std::string email;
  bool emailVerified = false;
  std::string login;
  std::string displayName;
  std::string avatarUrl;
};

struct PlayerRecord {
  std::string id;
  std::string username;  // login name, lower-cased for uniqueness
  std::string handle;    // public, 3-24 chars
  std::string displayName;
  std::string passwordHash;  // empty for OAuth-only accounts
  std::string email;         // private account email, lower-cased
  bool emailVerified = false;
  std::string createdAt;
  std::string updatedAt;
  std::vector<std::string> completedMissions;
  JVal flags = jobj();
  JVal appearance = jobj();  // {skin:[r,g,b],shirt:[...],pants:[...],hair:[...]}
  JVal identities = jarr();  // [{provider,subject,email,…}], storage only
  int homeUnit = 0;          // assigned apartment in the shared Home Row

  std::string toJson() const;        // storage (includes the hash)
  JVal publicJson() const;           // public API (never includes the hash/email)
  JVal selfJson() const;             // owner view: email + linked providers
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
  // Create a passwordless account owned by an external identity.
  bool createOAuth(const OAuthIdentity& identity, PlayerRecord* out, int* status,
                   std::string* err);
  // Attach an identity to an existing account. Re-linking the same provider id
  // is idempotent; a different subject for the same provider is a conflict.
  bool linkOAuth(PlayerRecord* rec, const OAuthIdentity& identity, int* status,
                 std::string* err);
  bool get(const std::string& id, PlayerRecord* out, bool* found,
           std::string* err);
  bool getByUsername(const std::string& username, PlayerRecord* out, bool* found,
                     std::string* err);
  bool getByHandle(const std::string& handle, PlayerRecord* out, bool* found,
                   std::string* err);
  bool getByOAuth(const std::string& provider, const std::string& subject,
                  PlayerRecord* out, bool* found, std::string* err);
  bool getByEmail(const std::string& email, PlayerRecord* out, bool* found,
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
  static std::string oauthKey(const std::string& provider,
                              const std::string& subject) {
    return "oauth:" + provider + ":" + subject;
  }
  static std::string emailKey(const std::string& email);
  static bool validProvider(const std::string& s);
  static std::string normalizeEmail(const std::string& s);

 private:
  static std::string toLowerCopy(const std::string& s);
  KvStore* kv_;
};

}  // namespace dw
