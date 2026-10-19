#include "social.h"

#include <algorithm>
#include <cctype>

namespace dw {

namespace {
bool has(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}
std::string lower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  return s;
}
}  // namespace

void Social::load(const JVal& j) {
  contacts_.clear();
  byId_.clear();
  for (const auto& c : j.at("contacts").arr) {
    ContactDef d;
    d.id = c.s("id");
    if (d.id.empty()) continue;
    d.name = c.s("name");
    d.role = c.s("role");
    d.cityId = c.s("cityId");
    d.skill = c.s("skill");
    d.blurb = c.s("blurb");
    byId_[d.id] = contacts_.size();
    contacts_.push_back(d);
  }
}

const ContactDef* Social::contact(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &contacts_[it->second];
}

std::string Social::reply(const ContactDef& c, const std::string& playerText) {
  std::string t = lower(playerText);
  if (has(t, "?")) return "Good question. Start by writing the smallest test you can make fail.";
  if (has(t, "help") || has(t, "stuck"))
    return "Send me the error, not the guesswork. Read the first line that says 'Error'.";
  if (has(t, "hire") || has(t, "team") || has(t, "join"))
    return "Me on your team? Sure — I'll review the diff, you ship it.";
  if (has(t, "hi") || has(t, "hello") || has(t, "hey"))
    return "Hey. " + c.name + " here — " + c.skill + " is my thing.";
  if (has(t, "thanks") || has(t, "thank"))
    return "Anytime. Now go break something small on purpose.";
  return "Noted. I'm around " + (c.cityId.empty() ? std::string("town") : c.cityId) +
         " if you want a second pair of eyes.";
}

void Social::exchange(Player& player, const ContactDef& c, const std::string& text,
                      const std::string& at, size_t cap) {
  Message out;
  out.contactId = c.id;
  out.fromThem = false;
  out.text = text;
  out.at = at;
  player.messages.push_back(out);

  Message in;
  in.contactId = c.id;
  in.fromThem = true;
  in.text = reply(c, text);
  in.at = at;
  player.messages.push_back(in);

  player.unread[c.id] += 1;

  if (player.messages.size() > cap)
    player.messages.erase(player.messages.begin(), player.messages.begin() + (player.messages.size() - cap));
}

std::vector<Message> Social::thread(const Player& player, const std::string& contactId) {
  std::vector<Message> out;
  for (const auto& m : player.messages)
    if (m.contactId == contactId) out.push_back(m);
  return out;
}

int Social::unreadCount(const Player& player) {
  int n = 0;
  for (const auto& kv : player.unread) n += kv.second;
  return n;
}

int Social::unreadFrom(const Player& player, const std::string& contactId) {
  auto it = player.unread.find(contactId);
  return it == player.unread.end() ? 0 : it->second;
}

void Social::markRead(Player& player, const std::string& contactId) {
  player.unread[contactId] = 0;
}

bool Social::addToTeam(Player& player, const std::string& contactId, std::string* reason) {
  if (std::find(player.team.begin(), player.team.end(), contactId) != player.team.end()) {
    if (reason) *reason = "Already on your team.";
    return false;
  }
  if ((int)player.team.size() >= kMaxTeam) {
    if (reason) *reason = "Your team is full (" + std::to_string(kMaxTeam) + ").";
    return false;
  }
  player.team.push_back(contactId);
  return true;
}

bool Social::removeFromTeam(Player& player, const std::string& contactId) {
  auto it = std::find(player.team.begin(), player.team.end(), contactId);
  if (it == player.team.end()) return false;
  player.team.erase(it);
  return true;
}

int Social::teamSkill(const Player& player, const std::vector<ContactDef>& known) {
  int n = 0;
  for (const auto& id : player.team)
    for (const auto& c : known)
      if (c.id == id) { n++; break; }
  return n;
}

}  // namespace dw
