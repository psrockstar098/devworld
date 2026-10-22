#pragma once
// skills.h — per-player mastery evidence storage + JSON views (Stage 4).
// Evidence is the only input to mastery (mastery.h); this module persists it
// in the durable kv store and renders the summaries the API serves.
// /skills/trees is deliberately NOT here: the tree comes from the curriculum
// catalog, which lands with the unlock-engine port.
#include <string>
#include <vector>

#include "json.h"
#include "kv.h"
#include "mastery.h"

namespace dw {

// kv key for a player's evidence list.
std::string evidenceStoreKey(const std::string& playerId);

// Evidence -> JSON (all fields; the store round-trips through parseEvidence).
JVal evidenceToJson(const Evidence& e);

// Loads the player's evidence. A missing key is an empty list, not an error.
// A corrupt envelope (unparseable, or not an array) is an error; individual
// entries that fail to parse are skipped, never fatal.
bool loadEvidence(KvStore* kv, const std::string& playerId,
                  std::vector<Evidence>* out, std::string* err);
bool saveEvidence(KvStore* kv, const std::string& playerId,
                  const std::vector<Evidence>& all, std::string* err);

// Appends one observation, deduplicated by evidenceKey (the same observation
// must not score twice). On success *updated holds the full list after the
// write (unchanged when *duplicate is true).
bool recordEvidence(KvStore* kv, const Evidence& e, bool* duplicate,
                    std::vector<Evidence>* updated, std::string* err);

// One skill's mastery summary for API responses.
JVal skillMasteryJson(const SkillMastery& m);

// GET /skills/me body: the player's skills plus the skills due for review.
JVal skillsMeJson(const std::string& playerId, const std::vector<Evidence>& all,
                  const MasteryConfig& cfg, int64_t nowMs);

}  // namespace dw
