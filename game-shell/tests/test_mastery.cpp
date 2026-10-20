// tests/test_mastery.cpp — the evidence-based mastery model (design 4.3).
//
// The point of these tests is the distinction the education spec insists on:
// passing the shipped tests scores well but is NOT mastery. Only independent
// evidence (transfer / delayed recall) + working code earns `mastered`.
#include "doctest.h"
#include "json.h"
#include "mastery.h"

#include <vector>

using namespace dw;

namespace {

const int64_t kDay = 24LL * 3600 * 1000;
const int64_t kNow = 1'000LL * kDay;

MasteryConfig cfg() { return MasteryConfig{}; }

std::vector<Evidence> justPublicTests(const std::string& skill) {
  std::vector<Evidence> ev;
  ev.push_back(makeEvidence("p1", skill, EvidenceKind::AutomatedTest, 1.0, kNow - kDay, "m02"));
  ev.push_back(makeEvidence("p1", skill, EvidenceKind::HiddenEdgeCase, 1.0, kNow - kDay, "m02"));
  return ev;
}

}  // namespace

TEST_CASE("evidence kind names round-trip and parse aliases") {
  for (int i = 0; i < (int)EvidenceKind::Count; i++) {
    EvidenceKind k = (EvidenceKind)i;
    EvidenceKind back;
    REQUIRE(evidenceKindFromString(evidenceKindName(k), back));
    CHECK(back == k);
  }
  EvidenceKind out;
  CHECK(evidenceKindFromString("Hidden-Edge-Case", out));
  CHECK(out == EvidenceKind::HiddenEdgeCase);
  CHECK(evidenceKindFromString(" transfer ", out));
  CHECK(out == EvidenceKind::Transfer);
  CHECK_FALSE(evidenceKindFromString("mind_reading", out));
}

TEST_CASE("decay halves at the half-life and never punishes the future") {
  CHECK(decayFactor(kNow, kNow, 10 * kDay) == doctest::Approx(1.0));
  CHECK(decayFactor(kNow - 10 * kDay, kNow, 10 * kDay) == doctest::Approx(0.5));
  CHECK(decayFactor(kNow - 20 * kDay, kNow, 10 * kDay) == doctest::Approx(0.25));
  CHECK(decayFactor(kNow + 5 * kDay, kNow, 10 * kDay) == doctest::Approx(1.0));  // clock skew
  CHECK(decayFactor(kNow - kDay, kNow, 0) == doctest::Approx(1.0));             // no decay configured
}

TEST_CASE("passing the shipped tests alone is not mastery") {
  auto ev = justPublicTests("loops.for_iteration");
  SkillMastery sm = summarizeSkill("loops.for_iteration", ev, cfg(), kNow);
  CHECK(sm.evidenceCount == 2);
  CHECK(sm.score > 0.9);          // the work is real and scores well
  CHECK_FALSE(sm.independent);    // but it is not independent evidence
  CHECK_FALSE(sm.mastered);
}

TEST_CASE("independent evidence plus working code earns mastery") {
  auto ev = justPublicTests("functions.return_values");
  ev.push_back(makeEvidence("p1", "functions.return_values", EvidenceKind::Transfer, 0.9,
                            kNow - kDay / 2, "m13"));
  SkillMastery sm = summarizeSkill("functions.return_values", ev, cfg(), kNow);
  CHECK(sm.independent);
  CHECK(sm.mastered);
  CHECK(sm.score >= cfg().masteryThreshold);
}

TEST_CASE("independent evidence without working code is not enough") {
  std::vector<Evidence> ev;
  ev.push_back(makeEvidence("p1", "sql.select_where", EvidenceKind::Transfer, 1.0, kNow, "m17"));
  SkillMastery sm = summarizeSkill("sql.select_where", ev, cfg(), kNow);
  CHECK_FALSE(sm.independent);  // nobody saw the code actually run
  CHECK_FALSE(sm.mastered);
}

TEST_CASE("a strong average with a weak transfer still fails independence") {
  auto ev = justPublicTests("data.mapping_keys");
  ev.push_back(makeEvidence("p1", "data.mapping_keys", EvidenceKind::Transfer, 0.4, kNow, "m16"));
  SkillMastery sm = summarizeSkill("data.mapping_keys", ev, cfg(), kNow);
  CHECK(sm.score > 0.6);
  CHECK_FALSE(sm.independent);  // transfer below the independent threshold
}

TEST_CASE("weighted score favours the more meaningful evidence") {
  std::vector<Evidence> ev;
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0, kNow, "m1"));
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::ProjectRubric, 0.0, kNow, "m2"));
  SkillMastery sm = summarizeSkill("s", ev, cfg(), kNow);
  // weight(AutomatedTest)=1.0, weight(ProjectRubric)=1.6
  CHECK(sm.score == doctest::Approx(1.0 / (1.0 + 1.6)));
}

TEST_CASE("weakest evidence is reported for practice") {
  auto ev = justPublicTests("s");
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::Explanation, 0.3, kNow, "m1"));
  SkillMastery sm = summarizeSkill("s", ev, cfg(), kNow);
  CHECK(sm.weakest == EvidenceKind::Explanation);
  CHECK(sm.weakestScore == doctest::Approx(0.3));
}

TEST_CASE("the weakest kind tracks the best score seen for that kind") {
  // Two automated tests at 0.2 and 0.9: the best is what counts, so Explanation
  // at 0.3 is still the weakest.
  auto ev = justPublicTests("s");
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 0.2, kNow, "m1a"));
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::Explanation, 0.3, kNow, "m1b"));
  SkillMastery sm = summarizeSkill("s", ev, cfg(), kNow);
  CHECK(sm.weakest == EvidenceKind::Explanation);
  CHECK(sm.weakestScore == doctest::Approx(0.3));
}

TEST_CASE("spaced review interval grows with consecutive recalls") {
  std::vector<Evidence> ev = justPublicTests("s");
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::DelayedRecall, 0.9, kNow - 2 * kDay, "r1"));
  SkillMastery once = summarizeSkill("s", ev, cfg(), kNow);
  CHECK(once.streak == 1);
  CHECK(once.retention <= 1.0);
  CHECK(once.retention > 0.0);
  // interval for streak 1 is 3 days from the newest evidence
  CHECK(once.dueAtMs == once.lastEvidenceMs + 3 * kDay);

  ev.push_back(makeEvidence("p1", "s", EvidenceKind::DelayedRecall, 0.9, kNow - kDay, "r2"));
  SkillMastery twice = summarizeSkill("s", ev, cfg(), kNow);
  CHECK(twice.streak == 2);
  CHECK(twice.dueAtMs == twice.lastEvidenceMs + 7 * kDay);

  ev.push_back(makeEvidence("p1", "s", EvidenceKind::DelayedRecall, 0.2, kNow, "r3"));
  SkillMastery reset = summarizeSkill("s", ev, cfg(), kNow);
  CHECK(reset.streak == 0);  // a failed recall resets the ladder
}

TEST_CASE("a skill with no evidence summarizes empty and is not mastered") {
  SkillMastery sm = summarizeSkill("nobody", {}, cfg(), kNow);
  CHECK(sm.evidenceCount == 0);
  CHECK(sm.score == 0);
  CHECK_FALSE(sm.independent);
  CHECK_FALSE(sm.mastered);
}

TEST_CASE("summarize groups by skill, sorted and unique") {
  std::vector<Evidence> ev = justPublicTests("b.topic");
  ev.push_back(makeEvidence("p1", "a.topic", EvidenceKind::AutomatedTest, 1.0, kNow, "m1"));
  ev.push_back(makeEvidence("p1", "a.topic", EvidenceKind::AutomatedTest, 1.0, kNow, "m2"));
  auto all = summarize(ev, cfg(), kNow);
  REQUIRE(all.size() == 2);
  CHECK(all[0].skill == "a.topic");
  CHECK(all[1].skill == "b.topic");
  CHECK(all[0].evidenceCount == 2);
}

TEST_CASE("dueForReview only returns evidence whose review is due") {
  // `due`: last exercised 40 days ago; the single recall sets a 3-day interval,
  // so its review is long overdue.
  std::vector<Evidence> ev;
  ev.push_back(makeEvidence("p1", "due", EvidenceKind::AutomatedTest, 1.0, kNow - 40 * kDay, "m"));
  ev.push_back(makeEvidence("p1", "due", EvidenceKind::DelayedRecall, 0.9, kNow - 70 * kDay, "r"));
  // `not_due`: just practised, so its interval has not elapsed.
  ev.push_back(makeEvidence("p1", "not_due", EvidenceKind::AutomatedTest, 1.0, kNow, "m"));
  auto due = dueForReview(ev, cfg(), kNow);
  REQUIRE(due.size() == 1);
  CHECK(due[0].skill == "due");
}

TEST_CASE("weakestSkills ranks unmastered skills by score") {
  std::vector<Evidence> ev = justPublicTests("strong");
  ev.push_back(makeEvidence("p1", "strong", EvidenceKind::Transfer, 1.0, kNow, "m13"));
  ev.push_back(makeEvidence("p1", "weak", EvidenceKind::Explanation, 0.2, kNow, "m1"));
  ev.push_back(makeEvidence("p1", "middling", EvidenceKind::AutomatedTest, 0.7, kNow, "m2"));
  auto weak = weakestSkills(ev, cfg(), kNow, 2);
  REQUIRE(weak.size() == 2);
  CHECK(weak[0].skill == "weak");
  CHECK(weak[1].skill == "middling");  // mastered "strong" is excluded
}

TEST_CASE("lastEvidenceAt finds the newest observation for a skill") {
  std::vector<Evidence> ev;
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::AutomatedTest, 1.0, 100, "m1"));
  ev.push_back(makeEvidence("p1", "s", EvidenceKind::HiddenEdgeCase, 1.0, 350, "m2"));
  ev.push_back(makeEvidence("p1", "other", EvidenceKind::Transfer, 1.0, 999, "m3"));
  CHECK(lastEvidenceAt(ev, "s") == 350);
  CHECK(lastEvidenceAt(ev, "other") == 999);
  CHECK(lastEvidenceAt(ev, "missing") == 0);
}

TEST_CASE("delayed recall needs a real gap and then earns independence") {
  // First exposure: shipped tests only.
  std::vector<Evidence> ev = justPublicTests("loops.for_iteration");
  CHECK_FALSE(summarizeSkill("loops.for_iteration", ev, cfg(), kNow).independent);

  // A revisit too soon is not a delayed recall.
  const int64_t soon = lastEvidenceAt(ev, "loops.for_iteration") + 60 * 1000;
  CHECK(soon - lastEvidenceAt(ev, "loops.for_iteration") < cfg().recallMinAgeMs);

  // A revisit after the minimum age is: it clears the independent bar.
  const int64_t later = lastEvidenceAt(ev, "loops.for_iteration") + cfg().recallMinAgeMs;
  ev.push_back(makeEvidence("p1", "loops.for_iteration", EvidenceKind::DelayedRecall, 1.0, later, "practice:m02"));
  SkillMastery sm = summarizeSkill("loops.for_iteration", ev, cfg(), later);
  CHECK(sm.independent);
}

TEST_CASE("evidenceKey is stable and ignores the score") {
  Evidence a = makeEvidence("p1", "s", EvidenceKind::Transfer, 0.5, 42, "m13");
  Evidence b = makeEvidence("p1", "s", EvidenceKind::Transfer, 0.9, 42, "m13");
  CHECK(evidenceKey(a) == evidenceKey(b));
  Evidence c = makeEvidence("p1", "s", EvidenceKind::Transfer, 0.9, 43, "m13");
  CHECK(evidenceKey(a) != evidenceKey(c));
}

TEST_CASE("parseEvidence reads the save shape and rejects garbage") {
  JVal j = parseJson(
      "{\"playerId\":\"p1\",\"skill\":\"loops.for_iteration\",\"kind\":\"hidden_edge_case\","
      "\"score\":0.75,\"atMs\":1234,\"source\":\"m06\",\"detail\":\"crates\"}");
  Evidence e;
  REQUIRE(parseEvidence(j, e));
  CHECK(e.playerId == "p1");
  CHECK(e.skill == "loops.for_iteration");
  CHECK(e.kind == EvidenceKind::HiddenEdgeCase);
  CHECK(e.score == doctest::Approx(0.75));
  CHECK(e.atMs == 1234);
  CHECK(e.source == "m06");

  Evidence ignored;
  CHECK_FALSE(parseEvidence(parseJson("{\"skill\":\"\",\"kind\":\"transfer\"}"), ignored));
  CHECK_FALSE(parseEvidence(parseJson("{\"skill\":\"s\",\"kind\":\"vibes\"}"), ignored));
}

TEST_CASE("scores are clamped to [0,1] by makeEvidence") {
  Evidence hi = makeEvidence("p", "s", EvidenceKind::Transfer, 2.0, 0, "");
  Evidence lo = makeEvidence("p", "s", EvidenceKind::Transfer, -1.0, 0, "");
  CHECK(hi.score == doctest::Approx(1.0));
  CHECK(lo.score == doctest::Approx(0.0));
}
