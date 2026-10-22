#pragma once
// frameworks.h — the framework catalog (design 13 §6.2).
//
// A framework entry is reference content: what it is, which language it
// belongs to, which discipline it serves, and which demonstrated
// competencies it requires. Availability is tied to prerequisites, never to
// level alone — a framework with no skills listed is available to everyone.
// Language may be empty for language-agnostic tooling (containers, package
// managers); a present language must be a known language id.
#include <map>
#include <string>
#include <vector>

#include "json.h"

namespace dw {

struct FrameworkDef {
  std::string id, name, language, discipline, blurb;
  int level = 0;
  std::vector<std::string> skills;  // curriculum topic ids
};

class Frameworks {
 public:
  void load(const JVal& j);  // content/frameworks.json
  bool empty() const { return defs_.empty(); }
  const std::vector<FrameworkDef>& all() const { return defs_; }
  const FrameworkDef* def(const std::string& id) const;
  // Discipline id -> display name, from the file's own disciplines list.
  const std::map<std::string, std::string>& disciplines() const {
    return disciplines_;
  }
  // Unique ids, non-empty name/discipline, discipline listed, level >= 0.
  std::vector<std::string> validate() const;

 private:
  std::vector<FrameworkDef> defs_;
  std::map<std::string, size_t> byId_;
  std::map<std::string, std::string> disciplines_;
};

}  // namespace dw
