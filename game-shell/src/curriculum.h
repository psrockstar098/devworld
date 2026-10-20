#pragma once
// curriculum.h — the knowledge graph behind the campaign (design 4.1).
//
// Twelve knowledge areas, each split into topics. A topic is the unit of
// mastery: its id is also the *skill tag* that missions award evidence for, so
// the curriculum, the mission content and the mastery model all speak the same
// vocabulary.
//
// A topic records what the spec asks an expert to author:
//   prerequisites, learning objectives, exercises (mission ids),
//   misconceptions with remedies and probes, and a difficulty.
//
// Pure: no SDL, no I/O, no time. The game loads curriculum.json, and the
// native + WASM test suites validate the shipped graph with the same code.
// Design: docs/design/08-education-system.md.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "json.h"

namespace dw {

struct KnowledgeArea {
  std::string id, title, summary;
};

struct Objective {
  std::string id, text;
};

// A wrong mental model a learner commonly holds. `probes` are mission ids that
// expose it; `remedy` is the corrective explanation or exercise.
struct Misconception {
  std::string id, text, remedy;
  std::vector<std::string> probes;
};

struct Topic {
  std::string id;      // also the skill tag, e.g. "variables.expressions"
  std::string area;    // KnowledgeArea.id
  std::string title;
  std::string summary;
  int difficulty = 1;  // 1..5
  std::vector<std::string> prerequisites;  // direct topic ids
  std::vector<Objective> objectives;
  std::vector<Misconception> misconceptions;
  std::vector<std::string> missions;  // shipped missions that teach it
  std::vector<std::string> projects;  // planned practical projects (no mission yet)
};

class Curriculum {
 public:
  void load(const JVal& curriculumJson);  // content/curriculum.json

  bool empty() const { return topics_.empty(); }
  const std::vector<KnowledgeArea>& areas() const { return areas_; }
  const std::map<std::string, Topic>& topics() const { return topics_; }
  const Topic* topic(const std::string& id) const;
  const KnowledgeArea* area(const std::string& id) const;
  std::vector<const Topic*> topicsInArea(const std::string& areaId) const;

  // ---- dependency graph ----
  std::vector<std::string> directPrerequisites(const std::string& topicId) const;
  // Transitive prerequisites, dependencies first, deterministic.
  std::vector<std::string> allPrerequisites(const std::string& topicId) const;
  // Prerequisites (in dependency order) followed by the topic itself.
  std::vector<std::string> pathTo(const std::string& topicId) const;
  // Every topic, dependencies first. Unknown ids are omitted.
  std::vector<std::string> topologicalOrder() const;

  // Topics whose prerequisites are all in `mastered`, excluding mastered ones.
  // Sorted by area order, then difficulty, then id.
  std::vector<std::string> availableTopics(const std::set<std::string>& mastered) const;
  std::vector<std::string> recommendedNext(const std::set<std::string>& mastered,
                                           size_t limit) const;

  // Which topics a mission teaches (reverse index over Topic::missions).
  std::vector<std::string> topicsForMission(const std::string& missionId) const;

  // Content errors. Empty means the graph is sound. `knownMissions` may be
  // empty to skip checking mission references (useful for a unit fixture).
  std::vector<std::string> validate(const std::set<std::string>& knownMissions = {}) const;

 private:
  std::vector<KnowledgeArea> areas_;
  std::map<std::string, Topic> topics_;
  std::vector<std::string> areaOrder_;
};

}  // namespace dw
