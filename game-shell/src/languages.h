#pragma once
// languages.h — the language catalog (design 13 deliverables).
//
// Each entry is one language the game cares about: its run tier (mirroring
// the IDE's language list, so the two cannot drift apart unnoticed), a
// blurb, and the disciplines it serves (the framework catalog's discipline
// vocabulary). This is the career-relevance layer; the IDE's languages.json
// remains the authority on extensions, highlighting and runners.
#include <map>
#include <string>
#include <vector>

#include "json.h"

namespace dw {

struct LanguageDef {
  std::string id, name, tier, blurb;
  std::vector<std::string> disciplines;  // framework-catalog discipline ids
};

class Languages {
 public:
  void load(const JVal& j);  // content/languages.json
  bool empty() const { return defs_.empty(); }
  const std::vector<LanguageDef>& all() const { return defs_; }
  const LanguageDef* def(const std::string& id) const;
  // Unique ids, non-empty name/tier, tier known, at least one discipline.
  std::vector<std::string> validate() const;
  // The four tiers, mirroring the IDE's run field.
  static bool isTier(const std::string& t);

 private:
  std::vector<LanguageDef> defs_;
  std::map<std::string, size_t> byId_;
};

}  // namespace dw
