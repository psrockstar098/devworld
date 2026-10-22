// test_rewards.cpp — XP is for meaningful work only (objective 5.4).
// Mirrors the shell's test_rewards.cpp against the server's MissionDef.
#include "doctest.h"
#include "rewards.h"

using namespace dw;

TEST_CASE("repetition, practice and code volume earn nothing") {
  MissionDef m;
  m.xpReward = 250;
  m.level = 4;
  CHECK(xpForEvent(RewardEvent::MissionFirstComplete, m).xp == 250);
  CHECK(xpForEvent(RewardEvent::RepeatCompletion, m).xp == 0);
  CHECK(xpForEvent(RewardEvent::PracticeRun, m).xp == 0);
  CHECK(xpForEvent(RewardEvent::CodeVolume, m).xp == 0);
}

TEST_CASE("meaningful events pay a positive bonus") {
  MissionDef m;
  m.xpReward = 200;
  for (RewardEvent e : {RewardEvent::GateComplete, RewardEvent::HiddenCaseFirst,
                        RewardEvent::DebugRecovery, RewardEvent::DelayedRecall,
                        RewardEvent::TransferFirst, RewardEvent::ProjectPublished,
                        RewardEvent::MentoredPeer, RewardEvent::Collaboration,
                        RewardEvent::ExplanationFirst})
    CHECK(xpForEvent(e, m).xp > 0);
}

TEST_CASE("social events have a floor even without a mission reward") {
  MissionDef m;  // xpReward 0
  CHECK(xpForEvent(RewardEvent::MentoredPeer, m).xp > 0);
  CHECK(xpForEvent(RewardEvent::Collaboration, m).xp > 0);
}

TEST_CASE("bonus percentages scale with the mission reward") {
  MissionDef m;
  m.xpReward = 200;
  CHECK(xpForEvent(RewardEvent::GateComplete, m).xp == 100);       // 50%
  CHECK(xpForEvent(RewardEvent::TransferFirst, m).xp == 50);       // 25%
  CHECK(xpForEvent(RewardEvent::ProjectPublished, m).xp == 40);   // 20%
  CHECK(xpForEvent(RewardEvent::DelayedRecall, m).xp == 30);       // 15%
  CHECK(xpForEvent(RewardEvent::HiddenCaseFirst, m).xp == 20);     // 10%
  CHECK(xpForEvent(RewardEvent::ExplanationFirst, m).xp == 10);    // 5%
}
