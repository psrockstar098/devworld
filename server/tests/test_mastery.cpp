#include "doctest.h"

#include "mastery.h"

using namespace dw;

namespace {
const int64_t kDay = 24LL * 3600 * 1000;
const int64_t kNow = 1000 * kDay;  // fixed clock: every result reproducible
MasteryConfig cfg;
}  // namespace

TEST_CASE("mastery: shipped tests alone never make a player independent") {
  // A player who only passes the shipped tests scores well but is neither
  // independent nor mastered. That is the Stage 4 completion criterion in code.
  std::vector<Evidence> ev = {
      makeEvidence("p1", "loops.accumulators", EvidenceKind::AutomatedTest, 1.0,
                   kNow - kDay, "m02"),
      makeEvidence("p1", "loops.accumulators", EvidenceKind::HiddenEdgeCase, 1.0,
                   kNow - kDay, "m02"),
  };
  SkillMastery sm = summarizeSkill("loops.accumulators", ev, cfg, kNow);
  CHECK(sm.score > 0.9);
  CHECK(sm.independent == false);
  CHECK(sm.mastered == false);
}

TEST_CASE("mastery: transfer plus working code earns independence and mastery") {
  std::vector<Evidence> ev = {
      makeEvidence("p1", "loops.accumulators", EvidenceKind::AutomatedTest, 1.0,
                   kNow - kDay, "m02"),
      makeEvidence("p1", "loops.accumulators", EvidenceKind::Transfer, 0.9,
                   kNow - kDay, "transfer-1"),
  };
  SkillMastery sm = summarizeSkill("loops.accumulators", ev, cfg, kNow);
  CHECK(sm.independent == true);
  CHECK(sm.mastered == true);
  // Transfer alone, without working code, is not independence either.
  std::vector<Evidence> onlyTransfer = {
      makeEvidence("p1", "loops.accumulators", EvidenceKind::Transfer, 1.0,
                   kNow - kDay, "transfer-1"),
  };
  SkillMastery sm2 = summarizeSkill("loops.accumulators", onlyTransfer, cfg, kNow);
  CHECK(sm2.independent == false);
  CHECK(sm2.mastered == false);
}

TEST_CASE("mastery: old evidence decays") {
  std::vector<Evidence> fresh = {
      makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0, kNow, "m"),
  };
  std::vector<Evidence> stale = {
      makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0,
                   kNow - 60 * kDay, "m"),
  };
  // A 60-day-old observation sits at exactly one half-life: its retention is
  // ~0.5 while a fresh one is 1.0.
  CHECK(summarizeSkill("s", fresh, cfg, kNow).retention == 1.0);
  double staleRetention = summarizeSkill("s", stale, cfg, kNow).retention;
  CHECK(staleRetention > 0.49);
  CHECK(staleRetention < 0.51);
  // Decay factor itself: future evidence does not decay.
  CHECK(decayFactor(kNow + kDay, kNow, cfg.retentionHalfLifeMs) == 1.0);
}

TEST_CASE("mastery: same observation never scores twice") {
  Evidence a = makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0, kNow, "m02");
  Evidence b = makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 0.5, kNow, "m02");
  // Score is excluded from the key: a re-grade of the same submission is the
  // same observation.
  CHECK(evidenceKey(a) == evidenceKey(b));
  Evidence c = makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0, kNow, "m03");
  CHECK(evidenceKey(a) != evidenceKey(c));
}

TEST_CASE("mastery: spaced review is due soonest first") {
  std::vector<Evidence> ev = {
      makeEvidence("p1", "old-skill", EvidenceKind::DelayedRecall, 1.0,
                   kNow - 40 * kDay, "m02"),
      makeEvidence("p1", "new-skill", EvidenceKind::AutomatedTest, 1.0,
                   kNow - kDay, "m02"),
  };
  std::vector<SkillMastery> due = dueForReview(ev, cfg, kNow);
  // Both are due: old-skill is 40 days past its last evidence, and new-skill
  // sits exactly on its 1-day review point. Soonest first puts old-skill ahead.
  REQUIRE(due.size() == 2);
  CHECK(due[0].skill == "old-skill");
  CHECK(due[1].skill == "new-skill");
}

TEST_CASE("mastery: weakest skills point at what to practise") {
  std::vector<Evidence> ev = {
      makeEvidence("p1", "strong", EvidenceKind::AutomatedTest, 1.0, kNow, "m"),
      makeEvidence("p1", "weak", EvidenceKind::AutomatedTest, 0.3, kNow, "m"),
  };
  std::vector<SkillMastery> weakest = weakestSkills(ev, cfg, kNow, 10);
  REQUIRE(weakest.size() == 2);
  CHECK(weakest[0].skill == "weak");
}

TEST_CASE("mastery: evidence parses from JSON and rejects bad kinds") {
  Evidence e;
  REQUIRE(parseEvidence(parseJson(R"({"playerId":"p1","skill":"s","kind":"transfer","score":0.8,"atMs":123,"source":"t1"})"),
                        e));
  CHECK(e.kind == EvidenceKind::Transfer);
  CHECK(e.score == 0.8);
  CHECK(e.atMs == 123);
  CHECK(!parseEvidence(parseJson(R"({"skill":"s","kind":"nope"})"), e));
  CHECK(!parseEvidence(parseJson(R"({"kind":"transfer"})"), e));
}
