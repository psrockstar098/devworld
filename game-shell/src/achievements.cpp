// achievements.cpp — see achievements.h.
#include "achievements.h"

#include <algorithm>
#include <set>

namespace dw {

void AchievementCatalog::load(const JVal& j) {
  defs_.clear();
  byId_.clear();
  for (const auto& a : j.at("achievements").arr) {
    AchievementDef d;
    d.id = a.s("id");
    if (d.id.empty()) continue;
    d.title = a.s("title", d.id);
    d.desc = a.s("desc");
    d.hidden = a.boolean("hidden");
    d.requires = parseRequirement(a.at("requires"));
    byId_[d.id] = defs_.size();
    defs_.push_back(d);
  }
}

const AchievementDef* AchievementCatalog::def(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &defs_[it->second];
}

std::vector<std::string> AchievementCatalog::earned(const PlayerCapability& cap,
                                                    const UnlockCatalog* cat,
                                                    const Curriculum* cur) const {
  std::vector<std::string> out;
  for (const auto& d : defs_)
    if (unmetRequirements(d.requires, cap, cat, cur).empty()) out.push_back(d.id);
  return out;
}

std::vector<std::string> AchievementCatalog::progress(const AchievementDef& d,
                                                      const PlayerCapability& cap,
                                                      const UnlockCatalog* cat,
                                                      const Curriculum* cur) const {
  return unmetRequirements(d.requires, cap, cat, cur);
}

std::vector<std::string> AchievementCatalog::validate() const {
  std::vector<std::string> errs;
  std::set<std::string> seen;
  for (const auto& d : defs_) {
    if (!seen.insert(d.id).second) errs.push_back("duplicate achievement id: " + d.id);
    if (d.title.empty()) errs.push_back("achievement " + d.id + " has no title");
    if (requirementEmpty(d.requires))
      errs.push_back("achievement " + d.id + " has no requirements (free achievements are noise)");
  }
  return errs;
}

std::vector<std::string> awardAchievements(Player& player, const AchievementCatalog& cat,
                                           const PlayerCapability& cap, const UnlockCatalog* ucat,
                                           const Curriculum* cur, std::string* firstNew) {
  std::vector<std::string> added;
  for (const auto& id : cat.earned(cap, ucat, cur)) {
    if (std::find(player.achievements.begin(), player.achievements.end(), id) !=
        player.achievements.end())
      continue;
    player.achievements.push_back(id);
    added.push_back(id);
  }
  if (firstNew && !added.empty()) *firstNew = added.front();
  return added;
}

}  // namespace dw
