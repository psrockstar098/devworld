// frameworks.cpp — see frameworks.h.
#include "frameworks.h"

#include <set>

namespace dw {

void Frameworks::load(const JVal& j) {
  defs_.clear();
  byId_.clear();
  disciplines_.clear();
  for (const auto& d : j.at("disciplines").arr) {
    std::string id = d.s("id");
    if (!id.empty()) disciplines_[id] = d.s("name", id);
  }
  for (const auto& f : j.at("frameworks").arr) {
    FrameworkDef d;
    d.id = f.s("id");
    if (d.id.empty()) continue;
    d.name = f.s("name", d.id);
    d.language = f.s("language");
    d.discipline = f.s("discipline");
    d.blurb = f.s("blurb");
    d.level = f.i("level", 0);
    const JVal& skills = f.at("skills");
    if (skills.type == JVal::ARR) {
      for (const auto& s : skills.arr)
        if (s.type == JVal::STR) d.skills.push_back(s.str);
    }
    byId_[d.id] = defs_.size();
    defs_.push_back(d);
  }
}

const FrameworkDef* Frameworks::def(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &defs_[it->second];
}

std::vector<std::string> Frameworks::validate() const {
  std::vector<std::string> errs;
  std::set<std::string> seen;
  for (const auto& d : defs_) {
    if (!seen.insert(d.id).second)
      errs.push_back("duplicate framework id: " + d.id);
    if (d.name.empty())
      errs.push_back("framework " + d.id + " has no name");
    if (d.discipline.empty()) {
      errs.push_back("framework " + d.id + " has no discipline");
    } else if (disciplines_.find(d.discipline) == disciplines_.end()) {
      errs.push_back("framework " + d.id + " discipline is not listed: " +
                     d.discipline);
    }
    if (d.level < 0)
      errs.push_back("framework " + d.id + " has a negative level");
  }
  return errs;
}

}  // namespace dw
