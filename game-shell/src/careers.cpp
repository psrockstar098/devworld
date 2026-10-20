// careers.cpp — see careers.h.
#include "careers.h"

#include <algorithm>
#include <set>

namespace dw {

namespace {

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += "; ";
    s += v[i];
  }
  return s;
}

}  // namespace

void Careers::load(const JVal& j) {
  defs_.clear();
  byId_.clear();
  for (const auto& p : j.at("paths").arr) {
    CareerDef d;
    d.id = p.s("id");
    if (d.id.empty()) continue;
    d.title = p.s("title", d.id);
    d.language = p.s("language");
    d.blurb = p.s("blurb");
    d.entry = parseRequirement(p.at("entry"));
    byId_[d.id] = defs_.size();
    defs_.push_back(d);
  }
}

const CareerDef* Careers::def(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &defs_[it->second];
}

std::vector<UnlockStatus> Careers::eligibility(const PlayerCapability& cap,
                                               const UnlockCatalog* cat,
                                               const Curriculum* cur) const {
  std::vector<UnlockStatus> out;
  for (const auto& d : defs_) {
    UnlockStatus st;
    st.id = d.id;
    st.met = metRequirements(d.entry, cap, cat, cur);
    st.missing = unmetRequirements(d.entry, cap, cat, cur);
    st.unlocked = st.missing.empty();
    out.push_back(st);
  }
  return out;
}

bool Careers::choose(Player& player, const CareerDef& d, const PlayerCapability& cap,
                     const UnlockCatalog* cat, const Curriculum* cur, std::string* reason) const {
  std::vector<std::string> missing = unmetRequirements(d.entry, cap, cat, cur);
  if (!missing.empty()) {
    if (reason) *reason = join(missing);
    return false;
  }
  player.career.path = d.id;
  if (player.career.history.empty() || player.career.history.back() != d.id)
    player.career.history.push_back(d.id);
  return true;
}

std::vector<std::string> Careers::validate() const {
  std::vector<std::string> errs;
  std::set<std::string> seen;
  for (const auto& d : defs_) {
    if (!seen.insert(d.id).second) errs.push_back("duplicate career id: " + d.id);
    if (d.title.empty()) errs.push_back("career " + d.id + " has no title");
    if (d.language.empty()) errs.push_back("career " + d.id + " has no language");
  }
  return errs;
}

}  // namespace dw
