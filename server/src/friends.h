#pragma once
// friends.h — real-user friendships, stored durably in Kvrocks.
//
// A friendship is mutual: both records carry the link, so either side can see
// it without scanning the other. Requests are kept on the recipient as
// `incoming` and on the sender as `outgoing`.
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"

namespace dw {

struct FriendLinks {
  std::vector<std::string> friends;
  std::vector<std::string> incoming;
  std::vector<std::string> outgoing;

  JVal toJson() const;
  static FriendLinks fromJson(const JVal& j);
};

class FriendStore {
 public:
  explicit FriendStore(KvStore* kv) : kv_(kv) {}

  bool get(const std::string& playerId, FriendLinks* out, std::string* err);
  bool save(const std::string& playerId, const FriendLinks& links,
            std::string* err);

  // All return false only on storage failure; `status` carries the HTTP code
  // for conflicts (400/404/409).
  bool request(const std::string& from, const std::string& to, int* status,
               std::string* err);
  bool accept(const std::string& playerId, const std::string& requester,
              int* status, std::string* err);
  bool decline(const std::string& playerId, const std::string& requester,
               int* status, std::string* err);
  bool remove(const std::string& a, const std::string& b, int* status,
              std::string* err);

  static std::string key(const std::string& playerId) {
    return "social:" + playerId;
  }

 private:
  KvStore* kv_;
};

}  // namespace dw
