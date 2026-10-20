// unlocks.cpp — unlock catalog + prerequisite engine (see unlocks.h).
#include "unlocks.h"

#include <algorithm>
#include <set>

namespace dw {

namespace {

const char* kKindNames[] = {"city_access",          "company_eligibility", "technical_privilege",
                            "equipment",            "job_tier",            "career"};

// A topic id is "<area>.<name>"; the area prefix is how content validation
// enforces "a technical privilege may only require its own area".
std::string areaOf(const std::string& topicId) {
  size_t dot = topicId.find('.');
  return dot == std::string::npos ? topicId : topicId.substr(0, dot);
}

std::string topicTitle(const Curriculum* cur, const std::string& id) {
  if (cur) {
    const Topic* t = cur->topic(id);
    if (t) return t->title;
  }
  return id;
}

std::vector<std::string> stringList(const JVal& v) {
  std::vector<std::string> out;
  for (const auto& e : v.arr)
    if (e.type == JVal::STR) out.push_back(e.str);
  return out;
}

UnlockRequirement parseRequires(const JVal& r) {
  UnlockRequirement q;
  q.minLevel = r.i("level");
  q.missions = stringList(r.at("missions"));
  q.gates = stringList(r.at("gates"));
  q.milestones = stringList(r.at("milestones"));
  q.skills = stringList(r.at("skills"));
  q.independentSkills = stringList(r.at("independentSkills"));
  q.minIndependent = r.i("minIndependent");
  q.minReliability = r.i("minReliability");
  q.requiresCompany = r.boolean("requiresCompany");
  q.minCompanyRep = r.i("minCompanyRep");
  return q;
}

bool noRequirements(const UnlockRequirement& q) {
  return q.minLevel == 0 && q.missions.empty() && q.gates.empty() && q.milestones.empty() &&
         q.skills.empty() && q.independentSkills.empty() && q.minIndependent == 0 &&
         q.minReliability == 0 && !q.requiresCompany && q.minCompanyRep == 0;
}

}  // namespace

// ---- shared requirement evaluator (used by unlocks and achievements) ----

std::vector<std::string> metRequirements(const UnlockRequirement& q, const PlayerCapability& cap,
                                         const UnlockCatalog* cat, const Curriculum* cur) {
  std::vector<std::string> met;
  if (q.minLevel > 0 && cap.level >= q.minLevel)
    met.push_back("Level " + std::to_string(q.minLevel) + " reached");
  for (const auto& m : q.missions)
    if (cap.has(cap.completedMissions, m)) met.push_back("Completed " + m);
  for (const auto& g : q.gates)
    if (cap.has(cap.completedGates, g)) met.push_back("Passed the gate " + g);
  for (const auto& ms : q.milestones) {
    if (cat && cat->milestoneEarned(ms, cap)) {
      const MilestoneDef* md = cat->milestone(ms);
      met.push_back("Milestone: " + std::string(md ? md->title : ms));
    }
  }
  for (const auto& s : q.skills)
    if (cap.has(cap.masteredSkills, s)) met.push_back("Mastered " + topicTitle(cur, s));
  for (const auto& s : q.independentSkills)
    if (cap.has(cap.independentSkills, s))
      met.push_back("Independent in " + topicTitle(cur, s));
  if (q.minIndependent > 0 && cap.independentCount >= q.minIndependent)
    met.push_back(std::to_string(q.minIndependent) + " independent assessments");
  if (q.minReliability > 0 && cap.reliability >= q.minReliability)
    met.push_back("Reliability " + std::to_string(cap.reliability));
  if (q.requiresCompany && cap.companyRank > 0) met.push_back("Company founded");
  if (q.minCompanyRep > 0 && cap.companyRank >= q.minCompanyRep)
    met.push_back("Company reputation " + std::to_string(cap.companyRank));
  return met;
}

std::vector<std::string> unmetRequirements(const UnlockRequirement& q, const PlayerCapability& cap,
                                           const UnlockCatalog* cat, const Curriculum* cur) {
  std::vector<std::string> missing;
  if (q.minLevel > 0 && cap.level < q.minLevel)
    missing.push_back("Reach level " + std::to_string(q.minLevel) + " (you are " +
                      std::to_string(cap.level) + ")");
  for (const auto& m : q.missions)
    if (!cap.has(cap.completedMissions, m)) missing.push_back("Complete mission " + m);
  for (const auto& g : q.gates)
    if (!cap.has(cap.completedGates, g))
      missing.push_back("Pass the gate " + g + " (no hints, hidden tests)");
  for (const auto& ms : q.milestones) {
    if (cat && cat->milestoneEarned(ms, cap)) continue;
    const MilestoneDef* md = cat ? cat->milestone(ms) : nullptr;
    missing.push_back("Earn the milestone: " + std::string(md ? md->title : ms));
  }
  for (const auto& s : q.skills)
    if (!cap.has(cap.masteredSkills, s)) missing.push_back("Master " + topicTitle(cur, s));
  for (const auto& s : q.independentSkills)
    if (!cap.has(cap.independentSkills, s))
      missing.push_back("Prove independent ability in " + topicTitle(cur, s));
  if (q.minIndependent > 0 && cap.independentCount < q.minIndependent)
    missing.push_back("Pass " + std::to_string(q.minIndependent) +
                      " independent assessments (you have " +
                      std::to_string(cap.independentCount) + ")");
  if (q.minReliability > 0 && cap.reliability < q.minReliability)
    missing.push_back("Reach reliability " + std::to_string(q.minReliability) + " (you are " +
                      std::to_string(cap.reliability) + ")");
  if (q.requiresCompany && cap.companyRank <= 0) missing.push_back("Found a company");
  if (q.minCompanyRep > 0 && cap.companyRank < q.minCompanyRep)
    missing.push_back("Company reputation " + std::to_string(q.minCompanyRep) + " required (you are " +
                      std::to_string(cap.companyRank) + ")");
  return missing;
}

bool requirementEmpty(const UnlockRequirement& q) {
  return q.minLevel == 0 && q.missions.empty() && q.gates.empty() && q.milestones.empty() &&
         q.skills.empty() && q.independentSkills.empty() && q.minIndependent == 0 &&
         q.minReliability == 0 && !q.requiresCompany && q.minCompanyRep == 0;
}

UnlockRequirement parseRequirement(const JVal& requiresJson) { return parseRequires(requiresJson); }

const char* unlockKindName(UnlockKind k) {
  int i = static_cast<int>(k);
  return (i < 0 || i >= 6) ? "unknown" : kKindNames[i];
}

bool unlockKindFromString(const std::string& s, UnlockKind& out) {
  for (int i = 0; i < 6; i++) {
    if (s == kKindNames[i]) {
      out = static_cast<UnlockKind>(i);
      return true;
    }
  }
  return false;
}

void UnlockCatalog::load(const JVal& j) {
  defs_.clear();
  byId_.clear();
  milestones_.clear();
  milestoneById_.clear();

  for (const auto& m : j.at("milestones").arr) {
    MilestoneDef md;
    md.id = m.s("id");
    md.title = m.s("title", md.id);
    md.mission = m.s("mission");
    if (md.id.empty()) continue;
    milestoneById_[md.id] = milestones_.size();
    milestones_.push_back(md);
  }

  for (const auto& u : j.at("unlocks").arr) {
    UnlockDef d;
    d.id = u.s("id");
    if (d.id.empty()) continue;
    d.title = u.s("title", d.id);
    d.blurb = u.s("blurb");
    UnlockKind k = UnlockKind::TechnicalPrivilege;
    if (unlockKindFromString(u.s("kind"), k)) d.kind = k;
    d.area = u.s("area");
    d.requires = parseRequires(u.at("requires"));
    byId_[d.id] = defs_.size();
    defs_.push_back(d);
  }
}

const UnlockDef* UnlockCatalog::def(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &defs_[it->second];
}

const MilestoneDef* UnlockCatalog::milestone(const std::string& id) const {
  auto it = milestoneById_.find(id);
  return it == milestoneById_.end() ? nullptr : &milestones_[it->second];
}

bool UnlockCatalog::milestoneEarned(const std::string& id, const PlayerCapability& cap) const {
  const MilestoneDef* md = milestone(id);
  if (!md || md->mission.empty()) return false;
  return cap.has(cap.completedMissions, md->mission);
}

const UnlockDef* UnlockCatalog::firstOfKind(UnlockKind kind) const {
  for (const auto& d : defs_)
    if (d.kind == kind) return &d;
  return nullptr;
}

UnlockStatus UnlockCatalog::evaluate(const UnlockDef& d, const PlayerCapability& cap,
                                     const Curriculum* cur) const {
  UnlockStatus st;
  st.id = d.id;
  st.met = metRequirements(d.requires, cap, this, cur);
  st.missing = unmetRequirements(d.requires, cap, this, cur);
  if (noRequirements(d.requires)) st.met.push_back("Open to everyone");
  st.unlocked = st.missing.empty();
  return st;
}

std::vector<UnlockStatus> UnlockCatalog::evaluateAll(const PlayerCapability& cap,
                                                     const Curriculum* cur) const {
  std::vector<const UnlockDef*> sorted;
  for (const auto& d : defs_) sorted.push_back(&d);
  std::sort(sorted.begin(), sorted.end(),
            [](const UnlockDef* a, const UnlockDef* b) { return a->id < b->id; });
  std::vector<UnlockStatus> out;
  out.reserve(sorted.size());
  for (const UnlockDef* d : sorted) out.push_back(evaluate(*d, cap, cur));
  return out;
}

std::vector<std::string> UnlockCatalog::validate() const {
  std::vector<std::string> errs;
  std::set<std::string> seen;
  for (const auto& d : defs_) {
    if (!seen.insert(d.id).second) errs.push_back("duplicate unlock id: " + d.id);

    if (d.kind == UnlockKind::TechnicalPrivilege) {
      if (d.area.empty()) errs.push_back("technical privilege " + d.id + " must name an area");
      // A privilege must rest on competency, not on level or XP alone.
      if (d.requires.skills.empty() && d.requires.independentSkills.empty() &&
          d.requires.gates.empty() && d.requires.milestones.empty())
        errs.push_back("technical privilege " + d.id +
                       " must require a competency (skill, gate or milestone), not level alone");
      // Only its own area's topics.
      auto checkArea = [&](const std::vector<std::string>& topics) {
        for (const auto& t : topics)
          if (!d.area.empty() && areaOf(t) != d.area)
            errs.push_back("technical privilege " + d.id + " requires unrelated topic " + t +
                           " (area " + areaOf(t) + " != " + d.area + ")");
      };
      checkArea(d.requires.skills);
      checkArea(d.requires.independentSkills);
    }

    // City access is breadth: never gated on a specific technology.
    if (d.kind == UnlockKind::CityAccess &&
        (!d.requires.skills.empty() || !d.requires.independentSkills.empty()))
      errs.push_back("city access " + d.id + " must not require a specific skill");

    for (const auto& ms : d.requires.milestones) {
      const MilestoneDef* md = milestone(ms);
      if (!md) errs.push_back("unlock " + d.id + " references unknown milestone " + ms);
      else if (md->mission.empty())
        errs.push_back("milestone " + ms + " has no mission");
    }
  }
  return errs;
}

}  // namespace dw
