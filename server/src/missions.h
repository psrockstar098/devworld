#pragma once
// missions.h — the mission catalogue (content) and the grading summary.
// Hidden test data never appears in a public mission response.
#include <map>
#include <string>
#include <vector>

#include "assess.h"
#include "json.h"

namespace dw {

struct MissionTest {
  std::string name;
  std::string stdin;
  std::string expected;
  bool hidden = false;
};

struct MissionDef {
  std::string id;
  std::string title;
  std::string language = "python";
  // One of missionFormats() (Stage 4, doc 08 §4.2). Absent in content means
  // "write-from-spec" — the format every shipped mission uses today.
  std::string format = "write-from-spec";
  std::string file;  // IDE path, e.g. missions/m01_first_boot.py
  int level = 1;
  std::vector<std::string> requires;
  int xpReward = 0;
  int creditReward = 0;
  bool gate = false;
  std::vector<MissionTest> tests;
};

// The 12 mission formats, in doc order. Content for the 11 non-default
// formats comes with the authoring slice; the catalogue validates the
// field today so bad content fails at load, not at play time.
inline const std::vector<std::string>& missionFormats() {
  static const std::vector<std::string> kFormats = {
    "predict", "complete", "write-from-spec", "debug", "refactor",
    "explain-failure", "optimize", "modify-unfamiliar", "design-first",
    "incomplete-requirements", "review-peer", "transfer-skill",
  };
  return kFormats;
}

inline bool isValidMissionFormat(const std::string& f) {
  for (const auto& x : missionFormats())
    if (x == f) return true;
  return false;
}

class MissionCatalog {
 public:
  bool loadFromJson(const JVal& j, std::string* err);
  bool loadFromFile(const std::string& path, std::string* err);
  const MissionDef* get(const std::string& id) const;
  size_t size() const { return missions_.size(); }
  const std::map<std::string, MissionDef>& all() const { return missions_; }

  JVal publicJson(const MissionDef& m) const;
  JVal listJson() const;

 private:
  std::map<std::string, MissionDef> missions_;
};

std::vector<AssessTest> assessTestsFor(const MissionDef& m);

struct MissionGrading {
  bool passed = false;
  int total = 0;
  int publicPassed = 0;
  int hiddenPassed = 0;
  int publicTests = 0;
  int hiddenTests = 0;
  bool rewarded = false;
  int xpReward = 0;
  int creditReward = 0;
  JVal results = jarr();
};

MissionGrading summarize(const MissionDef& m, const AssessResult& result);

}  // namespace dw
