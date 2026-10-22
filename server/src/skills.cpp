#include "skills.h"

namespace dw {

std::string evidenceStoreKey(const std::string& playerId) {
  return "skills:evidence:" + playerId;
}

JVal evidenceToJson(const Evidence& e) {
  JVal j = jobj();
  jset(j, "playerId", jstr(e.playerId));
  jset(j, "skill", jstr(e.skill));
  jset(j, "kind", jstr(evidenceKindName(e.kind)));
  jset(j, "score", jnum(e.score));
  jset(j, "atMs", jnum((double)e.atMs));
  jset(j, "source", jstr(e.source));
  jset(j, "detail", jstr(e.detail));
  return j;
}

bool loadEvidence(KvStore* kv, const std::string& playerId,
                  std::vector<Evidence>* out, std::string* err) {
  out->clear();
  std::string raw;
  bool found = false;
  if (!kv->get(evidenceStoreKey(playerId), &raw, &found, err)) return false;
  if (!found) return true;
  JVal j;
  try {
    j = parseJson(raw);
  } catch (const std::exception& e) {
    if (err) *err = std::string("corrupt evidence store: ") + e.what();
    return false;
  }
  if (j.type != JVal::ARR) {
    if (err) *err = "corrupt evidence store: not an array";
    return false;
  }
  for (const auto& item : j.arr) {
    Evidence e;
    if (parseEvidence(item, e)) out->push_back(std::move(e));
    // A bad entry is skipped, not fatal: one bad observation must not
    // wipe the player's whole skill history.
  }
  return true;
}

bool saveEvidence(KvStore* kv, const std::string& playerId,
                  const std::vector<Evidence>& all, std::string* err) {
  JVal arr = jarr();
  for (const auto& e : all) arr.arr.push_back(evidenceToJson(e));
  return kv->set(evidenceStoreKey(playerId), dumpJson(arr), 0, err);
}

bool recordEvidence(KvStore* kv, const Evidence& e, bool* duplicate,
                    std::vector<Evidence>* updated, std::string* err) {
  *duplicate = false;
  if (!loadEvidence(kv, e.playerId, updated, err)) return false;
  const std::string key = evidenceKey(e);
  for (const auto& old : *updated) {
    if (evidenceKey(old) == key) {
      *duplicate = true;
      return true;
    }
  }
  updated->push_back(e);
  return saveEvidence(kv, e.playerId, *updated, err);
}

JVal skillMasteryJson(const SkillMastery& m) {
  JVal j = jobj();
  jset(j, "skill", jstr(m.skill));
  jset(j, "score", jnum(m.score));
  jset(j, "retention", jnum(m.retention));
  jset(j, "independent", jbool(m.independent));
  jset(j, "mastered", jbool(m.mastered));
  jset(j, "evidenceCount", jnum(m.evidenceCount));
  jset(j, "weakest", jstr(evidenceKindName(m.weakest)));
  jset(j, "weakestScore", jnum(m.weakestScore));
  jset(j, "lastEvidenceMs", jnum((double)m.lastEvidenceMs));
  jset(j, "dueAtMs", jnum((double)m.dueAtMs));
  jset(j, "streak", jnum(m.streak));
  return j;
}

JVal skillsMeJson(const std::string& playerId, const std::vector<Evidence>& all,
                  const MasteryConfig& cfg, int64_t nowMs) {
  JVal j = jobj();
  jset(j, "playerId", jstr(playerId));
  JVal skills = jarr();
  for (const auto& m : summarize(all, cfg, nowMs))
    skills.arr.push_back(skillMasteryJson(m));
  jset(j, "skills", skills);
  JVal due = jarr();
  for (const auto& m : dueForReview(all, cfg, nowMs))
    due.arr.push_back(jstr(m.skill));
  jset(j, "dueForReview", due);
  return j;
}

}  // namespace dw
