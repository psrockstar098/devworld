#include "dungeons.h"

namespace dw {

void Dungeons::load(const JVal& j) {
  all_.clear();
  byId_.clear();
  for (const auto& d : j.at("dungeons").arr) {
    DungeonDef x;
    x.id = d.s("id");
    if (x.id.empty()) continue;
    x.name = d.s("name");
    x.blurb = d.s("blurb");
    x.cityId = d.s("cityId");
    x.entranceMap = d.s("entranceMap");
    x.interiorMap = d.s("interiorMap");
    x.entranceX = d.i("entranceX");
    x.entranceY = d.i("entranceY");
    x.minLevel = d.i("minLevel", 1);
    x.revealBy = d.s("revealBy", revealFlag(x.id));
    x.missionId = d.s("missionId");
    x.rewardItem = d.s("rewardItem");
    x.rewardText = d.s("rewardText");
    byId_[x.id] = all_.size();
    all_.push_back(x);
  }
}

const DungeonDef* Dungeons::byId(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &all_[it->second];
}

bool Dungeons::revealed(const DungeonDef& d,
                        const std::map<std::string, std::string>& flags) const {
  // If content names a custom flag, honour it; otherwise the standard one.
  std::string flag = d.revealBy.empty() ? revealFlag(d.id) : d.revealBy;
  auto it = flags.find(flag);
  return it != flags.end() && !it->second.empty() && it->second != "0" && it->second != "false";
}

std::vector<const DungeonDef*> Dungeons::atMap(
    const std::string& mapId, const std::map<std::string, std::string>& flags) const {
  std::vector<const DungeonDef*> out;
  for (const auto& d : all_)
    if (d.entranceMap == mapId && revealed(d, flags)) out.push_back(&d);
  return out;
}

bool Dungeons::canEnter(const DungeonDef& d, int level,
                        const std::map<std::string, std::string>& flags) const {
  return revealed(d, flags) && level >= d.minLevel;
}

std::string Dungeons::lockReason(const DungeonDef& d, int level,
                                 const std::map<std::string, std::string>& flags) const {
  if (!revealed(d, flags)) return "You have not found this place yet.";
  if (level < d.minLevel) return "Level " + std::to_string(d.minLevel) + " required";
  return "";
}

}  // namespace dw
