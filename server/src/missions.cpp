#include "missions.h"

#include <fstream>
#include <set>
#include <sstream>

#include "util.h"

namespace dw {

bool MissionCatalog::loadFromJson(const JVal& j, std::string* err) {
  const JVal* list = nullptr;
  if (j.type == JVal::ARR) list = &j;
  else if (j.type == JVal::OBJ && j.at("missions").type == JVal::ARR)
    list = &j.at("missions");
  if (!list) {
    if (err) *err = "missions.json must be an array or {missions:[]}";
    return false;
  }
  missions_.clear();
  std::set<std::string> seen;
  for (const auto& raw : list->arr) {
    if (raw.type != JVal::OBJ) {
      if (err) *err = "mission entry must be an object";
      return false;
    }
    MissionDef m;
    m.id = raw.s("id");
    if (m.id.empty()) {
      if (err) *err = "mission id is required";
      return false;
    }
    for (char c : m.id) {
      if (!(std::isalnum((unsigned char)c) || c == '_')) {
        if (err) *err = "bad mission id: " + m.id;
        return false;
      }
    }
    if (!seen.insert(m.id).second) {
      if (err) *err = "duplicate mission id: " + m.id;
      return false;
    }
    m.title = raw.s("title", m.id);
    m.language = raw.s("language", "python");
    m.file = raw.s("file");
    m.level = raw.i("level", 1);
    m.xpReward = raw.i("xpReward", 0);
    m.creditReward = raw.i("creditReward", 0);
    m.gate = raw.boolean("gate", false);
    const JVal& req = raw.at("requires");
    if (req.type == JVal::ARR) {
      for (const auto& r : req.arr)
        if (r.type == JVal::STR) m.requires.push_back(r.str);
    }
    const JVal& tests = raw.at("tests");
    if (tests.type != JVal::ARR || tests.arr.empty()) {
      if (err) *err = "mission " + m.id + " has no tests";
      return false;
    }
    for (const auto& t : tests.arr) {
      MissionTest mt;
      mt.name = t.s("name");
      mt.stdin = t.s("stdin");
      mt.expected = t.s("expected");
      mt.hidden = t.boolean("hidden", false);
      m.tests.push_back(std::move(mt));
    }
    missions_[m.id] = std::move(m);
  }
  return true;
}

bool MissionCatalog::loadFromFile(const std::string& path, std::string* err) {
  std::ifstream in(path);
  if (!in) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  try {
    JVal j = parseJson(ss.str());
    return loadFromJson(j, err);
  } catch (const std::exception& e) {
    if (err) *err = std::string("bad missions JSON: ") + e.what();
    return false;
  }
}

const MissionDef* MissionCatalog::get(const std::string& id) const {
  auto it = missions_.find(id);
  return it == missions_.end() ? nullptr : &it->second;
}

JVal MissionCatalog::publicJson(const MissionDef& m) const {
  JVal j = jobj();
  jset(j, "id", jstr(m.id));
  jset(j, "title", jstr(m.title));
  jset(j, "language", jstr(m.language));
  jset(j, "level", jnum(m.level));
  jset(j, "gate", jbool(m.gate));
  jset(j, "xpReward", jnum(m.xpReward));
  jset(j, "creditReward", jnum(m.creditReward));
  JVal req = jarr();
  for (const auto& r : m.requires) req.arr.push_back(jstr(r));
  jset(j, "requires", req);
  JVal tests = jarr();
  for (const auto& t : m.tests) {
    JVal tj = jobj();
    jset(tj, "name", jstr(t.name));
    jset(tj, "hidden", jbool(t.hidden));
    if (!t.hidden) {
      jset(tj, "stdin", jstr(t.stdin));
      jset(tj, "expected", jstr(t.expected));
    }
    tests.arr.push_back(std::move(tj));
  }
  jset(j, "tests", tests);
  return j;
}

JVal MissionCatalog::listJson() const {
  JVal arr = jarr();
  for (const auto& kv : missions_) arr.arr.push_back(publicJson(kv.second));
  JVal out = jobj();
  jset(out, "missions", arr);
  return out;
}

std::vector<AssessTest> assessTestsFor(const MissionDef& m) {
  std::vector<AssessTest> out;
  out.reserve(m.tests.size());
  for (const auto& t : m.tests) {
    AssessTest a;
    a.name = t.name;
    a.stdin = t.stdin;
    a.expectedStdout = t.expected;
    a.hidden = t.hidden;
    out.push_back(std::move(a));
  }
  return out;
}

MissionGrading summarize(const MissionDef& m, const AssessResult& result) {
  MissionGrading g;
  std::map<std::string, const AssessTestResult*> byName;
  for (const auto& r : result.results) byName[r.name] = &r;

  for (const auto& t : m.tests) {
    auto it = byName.find(t.name);
    bool passed = it != byName.end() && it->second->passed;
    JVal rj = jobj();
    jset(rj, "name", jstr(t.name));
    jset(rj, "passed", jbool(passed));
    jset(rj, "hidden", jbool(t.hidden));
    if (!t.hidden && it != byName.end()) {
      jset(rj, "actual", jstr(it->second->actual));
    }
    if (it != byName.end() && !it->second->errorKind.empty())
      jset(rj, "errorKind", jstr(it->second->errorKind));
    g.results.arr.push_back(std::move(rj));

    if (t.hidden) {
      g.hiddenTests++;
      if (passed) g.hiddenPassed++;
    } else {
      g.publicTests++;
      if (passed) g.publicPassed++;
    }
  }
  g.total = (int)m.tests.size();
  g.passed = g.publicPassed == g.publicTests && g.hiddenPassed == g.hiddenTests;
  g.rewarded = g.passed;
  if (g.passed) {
    g.xpReward = m.xpReward;
    g.creditReward = m.creditReward;
  }
  return g;
}

}  // namespace dw
