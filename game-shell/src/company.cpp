#include "company.h"

#include <algorithm>
#include <cctype>

namespace dw {

const std::vector<std::string>& Company::focuses() {
  static const std::vector<std::string> f = {
      "Contract Studio", "Product Lab", "Infra Shop", "Research Group"};
  return f;
}

bool Company::validName(const std::string& name, std::string* reason) {
  auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (name.size() < 3) return fail("Name is too short (3-24 characters).");
  if (name.size() > 24) return fail("Name is too long (3-24 characters).");
  bool letter = false;
  for (unsigned char c : name) {
    if (std::isalpha(c)) { letter = true; continue; }
    if (std::isdigit(c) || c == ' ' || c == '_') continue;
    return fail("Use letters, digits, spaces or underscores only.");
  }
  if (!letter) return fail("Name needs at least one letter.");
  return true;
}

bool Company::validFocus(const std::string& focus) {
  const auto& f = focuses();
  return std::find(f.begin(), f.end(), focus) != f.end();
}

std::string Company::focusBlurb(const std::string& focus) {
  if (focus == "Contract Studio") return "You take whatever ships: small builds, bug hunts, retainer work.";
  if (focus == "Product Lab") return "You build your own product and dogfood it in the world.";
  if (focus == "Infra Shop") return "You sell plumbing: deployments, pipelines, reliability.";
  if (focus == "Research Group") return "You chase hard problems first and monetise them later.";
  return "A scrappy outfit still deciding what it is.";
}

std::string Company::rankFor(int reputation) {
  if (reputation >= 400) return "Industry Name";
  if (reputation >= 200) return "Established Agency";
  if (reputation >= 80) return "Growing Studio";
  if (reputation >= 20) return "Garage Studio";
  return "Two Desks and a Kettle";
}

bool Company::found(Player& player, const std::string& name, const std::string& focus,
                    std::string* reason) {
  if (player.company.founded) {
    if (reason) *reason = "You already run " + player.company.name + ".";
    return false;
  }
  if (!validName(name, reason)) return false;
  if (!validFocus(focus)) {
    if (reason) *reason = "Pick one of the four focus areas.";
    return false;
  }
  player.company = CompanyState{};
  player.company.founded = true;
  player.company.name = name;
  player.company.focus = focus;
  return true;
}

int Company::reputationFrom(const std::map<std::string, MissionProgress>& missions,
                            int contracts) {
  int rep = 0;
  for (const auto& kv : missions)
    if (kv.second.state == MissionState::Complete) rep += 5;  // per completed project
  rep += contracts * 3;
  return rep;
}

CompanyInfo Company::info(const Player& player) {
  CompanyInfo out;
  if (!player.company.founded) return out;
  out.founded = true;
  out.name = player.company.name;
  out.focus = player.company.focus;
  out.focusBlurb = focusBlurb(player.company.focus);
  out.reputation = player.company.reputation;
  out.rank = rankFor(out.reputation);
  out.contracts = player.company.contracts;
  out.members = (int)player.company.members.size();
  return out;
}

int64_t Company::contractPayout(int64_t base, int reputation, int teamSize) {
  // +1% per 10 reputation, +10% per teammate beyond the founder, capped at +150%.
  int bonus = (int)(reputation / 10) * 1 + std::max(0, teamSize - 1) * 10;
  bonus = std::min(bonus, 150);
  return std::max<int64_t>(1, base * (100 + bonus) / 100);
}

}  // namespace dw
