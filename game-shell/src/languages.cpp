// languages.cpp — see languages.h.
#include "languages.h"

#include <set>

namespace dw {

bool Languages::isTier(const std::string& t) {
  return t == "runs-now" || t == "soon" || t == "sandbox" ||
         t == "editor-only";
}

void Languages::load(const JVal& j) {
  defs_.clear();
  byId_.clear();
  for (const auto& l : j.at("languages").arr) {
    LanguageDef d;
    d.id = l.s("id");
    if (d.id.empty()) continue;
    d.name = l.s("name", d.id);
    d.tier = l.s("tier");
    d.blurb = l.s("blurb");
    const JVal& ds = l.at("disciplines");
    if (ds.type == JVal::ARR) {
      for (const auto& x : ds.arr)
        if (x.type == JVal::STR) d.disciplines.push_back(x.str);
    }
    byId_[d.id] = defs_.size();
    defs_.push_back(d);
  }
}

const LanguageDef* Languages::def(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &defs_[it->second];
}

std::vector<std::string> Languages::validate() const {
  std::vector<std::string> errs;
  std::set<std::string> seen;
  for (const auto& d : defs_) {
    if (!seen.insert(d.id).second)
      errs.push_back("duplicate language id: " + d.id);
    if (d.name.empty())
      errs.push_back("language " + d.id + " has no name");
    if (!isTier(d.tier))
      errs.push_back("language " + d.id + " has unknown tier: " + d.tier);
    if (d.disciplines.empty())
      errs.push_back("language " + d.id + " serves no discipline");
  }
  return errs;
}

}  // namespace dw
