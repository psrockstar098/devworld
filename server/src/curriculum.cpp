// curriculum.h — the knowledge graph (see curriculum.h).
#include "curriculum.h"

#include <algorithm>
#include <functional>

namespace dw {

void Curriculum::load(const JVal& j) {
  areas_.clear();
  topics_.clear();
  areaOrder_.clear();

  for (const auto& a : j.at("areas").arr) {
    KnowledgeArea area;
    area.id = a.s("id");
    area.title = a.s("title");
    area.summary = a.s("summary");
    if (area.id.empty()) continue;
    areas_.push_back(area);
    areaOrder_.push_back(area.id);
  }

  for (const auto& t : j.at("topics").arr) {
    Topic topic;
    topic.id = t.s("id");
    topic.area = t.s("area");
    topic.title = t.s("title");
    topic.summary = t.s("summary");
    topic.difficulty = t.i("difficulty", 1);
    for (const auto& p : t.at("prerequisites").arr) topic.prerequisites.push_back(p.str);
    for (const auto& o : t.at("objectives").arr) {
      Objective obj;
      // Objectives may be plain strings or {id,text} objects.
      if (o.type == JVal::STR) {
        obj.text = o.str;
        obj.id = topic.id + "." + std::to_string(topic.objectives.size() + 1);
      } else {
        obj.id = o.s("id");
        obj.text = o.s("text");
      }
      topic.objectives.push_back(obj);
    }
    for (const auto& m : t.at("misconceptions").arr) {
      Misconception mc;
      mc.id = m.s("id");
      mc.text = m.s("text");
      mc.remedy = m.s("remedy");
      for (const auto& p : m.at("probes").arr) mc.probes.push_back(p.str);
      topic.misconceptions.push_back(mc);
    }
    for (const auto& m : t.at("missions").arr) topic.missions.push_back(m.str);
    for (const auto& p : t.at("projects").arr) topic.projects.push_back(p.str);
    if (topic.id.empty()) continue;
    topics_[topic.id] = topic;
  }
}

const Topic* Curriculum::topic(const std::string& id) const {
  auto it = topics_.find(id);
  return it == topics_.end() ? nullptr : &it->second;
}

const KnowledgeArea* Curriculum::area(const std::string& id) const {
  for (const auto& a : areas_)
    if (a.id == id) return &a;
  return nullptr;
}

std::vector<const Topic*> Curriculum::topicsInArea(const std::string& areaId) const {
  std::vector<const Topic*> out;
  for (const auto& kv : topics_)
    if (kv.second.area == areaId) out.push_back(&kv.second);
  std::sort(out.begin(), out.end(), [](const Topic* a, const Topic* b) {
    if (a->difficulty != b->difficulty) return a->difficulty < b->difficulty;
    return a->id < b->id;
  });
  return out;
}

std::vector<std::string> Curriculum::directPrerequisites(const std::string& topicId) const {
  const Topic* t = topic(topicId);
  if (!t) return {};
  std::vector<std::string> out;
  for (const auto& p : t->prerequisites)
    if (topics_.count(p)) out.push_back(p);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::string> Curriculum::topologicalOrder() const {
  std::map<std::string, int> indegree;
  for (const auto& kv : topics_) {
    int n = 0;
    for (const auto& p : kv.second.prerequisites)
      if (topics_.count(p)) n++;
    indegree[kv.first] = n;
  }

  std::set<std::string> ready;
  for (const auto& kv : indegree)
    if (kv.second == 0) ready.insert(kv.first);

  std::vector<std::string> order;
  while (!ready.empty()) {
    std::string id = *ready.begin();
    ready.erase(ready.begin());
    order.push_back(id);
    for (const auto& kv : topics_) {
      if (kv.second.prerequisites.end() ==
          std::find(kv.second.prerequisites.begin(), kv.second.prerequisites.end(), id))
        continue;
      if (--indegree[kv.first] == 0) ready.insert(kv.first);
    }
  }

  // A cycle leaves nodes with a positive indegree. Append them so the result is
  // still a total order; validate() is what reports the cycle.
  if (order.size() != topics_.size()) {
    for (const auto& kv : topics_)
      if (std::find(order.begin(), order.end(), kv.first) == order.end())
        order.push_back(kv.first);
  }
  return order;
}

std::vector<std::string> Curriculum::allPrerequisites(const std::string& topicId) const {
  if (!topic(topicId)) return {};
  std::set<std::string> need;
  std::vector<std::string> stack{topicId};
  while (!stack.empty()) {
    std::string id = stack.back();
    stack.pop_back();
    const Topic* t = topic(id);
    if (!t) continue;
    for (const auto& p : t->prerequisites) {
      if (!topics_.count(p)) continue;
      if (need.insert(p).second) stack.push_back(p);
    }
  }
  std::vector<std::string> out;
  for (const auto& id : topologicalOrder())
    if (need.count(id)) out.push_back(id);
  return out;
}

std::vector<std::string> Curriculum::pathTo(const std::string& topicId) const {
  if (!topic(topicId)) return {};
  std::vector<std::string> out = allPrerequisites(topicId);
  out.push_back(topicId);
  return out;
}

std::vector<std::string> Curriculum::availableTopics(const std::set<std::string>& mastered) const {
  std::vector<std::string> out;
  for (const auto& areaId : areaOrder_) {
    for (const Topic* t : topicsInArea(areaId)) {
      if (mastered.count(t->id)) continue;
      bool ok = true;
      for (const auto& p : t->prerequisites) {
        if (!topics_.count(p)) continue;  // reported by validate()
        if (!mastered.count(p)) {
          ok = false;
          break;
        }
      }
      if (ok) out.push_back(t->id);
    }
  }
  return out;
}

std::vector<std::string> Curriculum::recommendedNext(const std::set<std::string>& mastered,
                                                     size_t limit) const {
  std::vector<std::string> out = availableTopics(mastered);
  if (out.size() > limit) out.resize(limit);
  return out;
}

std::vector<std::string> Curriculum::topicsForMission(const std::string& missionId) const {
  std::vector<std::string> out;
  for (const auto& kv : topics_)
    if (std::find(kv.second.missions.begin(), kv.second.missions.end(), missionId) !=
        kv.second.missions.end())
      out.push_back(kv.first);
  return out;
}

std::vector<std::string> Curriculum::validate(const std::set<std::string>& knownMissions) const {
  std::vector<std::string> errs;

  std::set<std::string> areaIds;
  for (const auto& a : areas_) {
    if (!areaIds.insert(a.id).second) errs.push_back("duplicate area: " + a.id);
    if (a.title.empty()) errs.push_back("area " + a.id + ": missing title");
  }

  std::set<std::string> objectiveIds;
  std::set<std::string> misconceptionIds;
  for (const auto& kv : topics_) {
    const Topic& t = kv.second;
    if (t.area.empty() || !areaIds.count(t.area))
      errs.push_back("topic " + t.id + ": unknown area " + t.area);
    if (t.title.empty()) errs.push_back("topic " + t.id + ": missing title");
    if (t.difficulty < 1 || t.difficulty > 5)
      errs.push_back("topic " + t.id + ": difficulty must be 1..5");

    for (const auto& p : t.prerequisites) {
      if (p == t.id) errs.push_back("topic " + t.id + ": depends on itself");
      else if (!topics_.count(p))
        errs.push_back("topic " + t.id + ": unknown prerequisite " + p);
    }

    if (t.objectives.empty())
      errs.push_back("topic " + t.id + ": no learning objectives");
    for (const auto& o : t.objectives) {
      if (!objectiveIds.insert(o.id).second)
        errs.push_back("duplicate objective: " + o.id);
      if (o.text.empty()) errs.push_back("objective " + o.id + ": missing text");
    }

    for (const auto& m : t.misconceptions) {
      if (!misconceptionIds.insert(m.id).second)
        errs.push_back("duplicate misconception: " + m.id);
      if (m.text.empty()) errs.push_back("misconception " + m.id + ": missing text");
      if (m.remedy.empty()) errs.push_back("misconception " + m.id + ": missing remedy");
      for (const auto& probe : m.probes)
        if (!knownMissions.empty() && !knownMissions.count(probe))
          errs.push_back("misconception " + m.id + ": unknown probe mission " + probe);
    }

    for (const auto& mission : t.missions)
      if (!knownMissions.empty() && !knownMissions.count(mission))
        errs.push_back("topic " + t.id + ": unknown mission " + mission);

    if (t.missions.empty() && t.projects.empty())
      errs.push_back("topic " + t.id + ": no practical project or mission");
  }

  // Cycle detection (DFS with colours), reporting one concrete path per cycle.
  std::map<std::string, int> colour;  // 0 white, 1 grey, 2 black
  std::vector<std::string> stack;
  std::set<std::string> reported;
  std::function<void(const std::string&)> dfs = [&](const std::string& id) {
    colour[id] = 1;
    stack.push_back(id);
    for (const auto& p : topics_.at(id).prerequisites) {
      if (!topics_.count(p)) continue;
      if (colour[p] == 1) {
        std::string path;
        bool on = false;
        for (const auto& s : stack) {
          if (s == p) on = true;
          if (on) path += s + " -> ";
        }
        path += p;
        if (reported.insert(path).second) errs.push_back("cycle: " + path);
      } else if (colour[p] == 0) {
        dfs(p);
      }
    }
    stack.pop_back();
    colour[id] = 2;
  };
  for (const auto& kv : topics_)
    if (colour[kv.first] == 0) dfs(kv.first);

  return errs;
}

}  // namespace dw
