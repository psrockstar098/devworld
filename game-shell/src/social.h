#pragma once
// social.h — contacts, messaging and teams (design 2.3).
//
// Phase 1 has no network: contacts are in-world developer characters with
// deterministic, keyword-driven replies. The data shape and the reply rules are
// real and tested; Phase 2 swaps `reply()` for the messaging service and keeps
// the same panels. [PHASE2] markers sit where the server takes over.
//
// Pure: no SDL, no I/O. Content lives in content/contacts.json.

#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct ContactDef {
  std::string id, name, role, cityId, skill, blurb;
};

class Social {
 public:
  void load(const JVal& j);
  const std::vector<ContactDef>& contacts() const { return contacts_; }
  const ContactDef* contact(const std::string& id) const;

  // Deterministic, keyword-driven reply from a contact. Same input -> same
  // output, so it is unit-testable (and honest: nobody pretends it is a person).
  static std::string reply(const ContactDef& c, const std::string& playerText);

  // Record an outgoing message and the contact's reply; bumps unread.
  static void exchange(Player& player, const ContactDef& c, const std::string& text,
                       const std::string& at, size_t cap = 200);

  static std::vector<Message> thread(const Player& player, const std::string& contactId);
  static int unreadCount(const Player& player);
  static int unreadFrom(const Player& player, const std::string& contactId);
  static void markRead(Player& player, const std::string& contactId);

  // A team is up to kMaxTeam contacts, no duplicates.
  static constexpr int kMaxTeam = 3;
  static bool addToTeam(Player& player, const std::string& contactId, std::string* reason = nullptr);
  static bool removeFromTeam(Player& player, const std::string& contactId);
  static int teamSkill(const Player& player, const std::vector<ContactDef>& known);

 private:
  std::vector<ContactDef> contacts_;
  std::map<std::string, size_t> byId_;
};

}  // namespace dw
