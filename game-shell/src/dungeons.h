#pragma once
// dungeons.h — hidden technology dungeons (design 2.3: secret underground labs,
// abandoned data centers, forgotten code repositories, experimental machines).
//
// A dungeon is a map with a secret entrance. It stays invisible until the
// player *discovers* it (a flag set by an NPC rumour or an inspectable), then
// opens when the player meets its level. Inside, the payoff is a real
// programming challenge (a mission) and a piece of rare technology.
//
// Pure: no SDL, no I/O. Content lives in content/dungeons.json.

#include <map>
#include <string>
#include <vector>
#include "types.h"

namespace dw {

struct DungeonDef {
  std::string id, name, blurb;
  std::string cityId;
  std::string entranceMap;          // exterior map the hatch sits on
  std::string interiorMap;          // the secret room behind the hatch
  int entranceX = 0, entranceY = 0; // tile coords of the entrance
  int minLevel = 1;
  std::string revealBy;             // flag needed: "dungeon_<id>" once found
  std::string missionId;            // the programming challenge inside
  std::string rewardItem;           // rare tech granted on completion (shop id)
  std::string rewardText;
};

class Dungeons {
 public:
  void load(const JVal& j);
  const std::vector<DungeonDef>& all() const { return all_; }
  const DungeonDef* byId(const std::string& id) const;

  // Discovered once its reveal flag is set to anything truthy.
  bool revealed(const DungeonDef& d, const std::map<std::string, std::string>& flags) const;

  // Visible entrances on a map (revealed only — secrets stay secret).
  std::vector<const DungeonDef*> atMap(const std::string& mapId,
                                       const std::map<std::string, std::string>& flags) const;

  // Enterable when revealed and the level gate is met.
  bool canEnter(const DungeonDef& d, int level,
                const std::map<std::string, std::string>& flags) const;

  // Human reason when canEnter is false ("" when it is true).
  std::string lockReason(const DungeonDef& d, int level,
                         const std::map<std::string, std::string>& flags) const;

  static std::string revealFlag(const std::string& id) { return "dungeon_" + id; }
  static std::string doneFlag(const std::string& id) { return "dungeon_cleared_" + id; }

 private:
  std::vector<DungeonDef> all_;
  std::map<std::string, size_t> byId_;
};

}  // namespace dw
