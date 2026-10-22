// rewards.cpp — see rewards.h.
#include "rewards.h"

#include <algorithm>

namespace dw {

namespace {

const char* kEventNames[] = {
    "mission_first_complete", "gate_complete",  "hidden_case_first", "explanation_first",
    "debug_recovery",         "delayed_recall", "transfer_first",    "project_published",
    "mentored_peer",          "collaboration",  "repeat_completion", "practice_run",
    "code_volume"};

// Percentage of the mission's own reward for a bonus event.
int percentFor(RewardEvent e) {
  switch (e) {
    case RewardEvent::GateComplete: return 50;
    case RewardEvent::TransferFirst: return 25;
    case RewardEvent::ProjectPublished: return 20;
    case RewardEvent::DelayedRecall: return 15;
    case RewardEvent::HiddenCaseFirst: return 10;
    case RewardEvent::DebugRecovery: return 10;
    case RewardEvent::ExplanationFirst: return 5;
    default: return 0;
  }
}

int64_t floorFor(RewardEvent e) {
  switch (e) {
    case RewardEvent::MentoredPeer:
    case RewardEvent::Collaboration: return 50;
    default: return 0;
  }
}

}  // namespace

const char* rewardEventName(RewardEvent e) {
  int i = static_cast<int>(e);
  return (i < 0 || i >= 13) ? "unknown" : kEventNames[i];
}

XpAward xpForEvent(RewardEvent e, const MissionDef& mission) {
  XpAward a;
  switch (e) {
    // The "avoid" list: these events exist to be worth nothing.
    case RewardEvent::RepeatCompletion:
      a.xp = 0;
      a.reason = "Repeating finished work earns nothing.";
      return a;
    case RewardEvent::PracticeRun:
      a.xp = 0;
      a.reason = "Practice is its own reward.";
      return a;
    case RewardEvent::CodeVolume:
      a.xp = 0;
      a.reason = "Quantity of code is never rewarded.";
      return a;
    default: break;
  }

  if (e == RewardEvent::MissionFirstComplete) {
    a.xp = mission.xpReward;
    a.reason = "Completed meaningful work.";
    return a;
  }

  int pct = percentFor(e);
  int64_t xp = mission.xpReward * pct / 100;
  xp = std::max(xp, floorFor(e));
  a.xp = xp;
  switch (e) {
    case RewardEvent::GateComplete: a.reason = "Demonstrated mastery without assistance."; break;
    case RewardEvent::HiddenCaseFirst: a.reason = "Solved cases you had never seen."; break;
    case RewardEvent::ExplanationFirst: a.reason = "Explained why it works."; break;
    case RewardEvent::DebugRecovery: a.reason = "Found and repaired a defect."; break;
    case RewardEvent::DelayedRecall: a.reason = "Recalled a skill later, unaided."; break;
    case RewardEvent::TransferFirst: a.reason = "Applied a skill in a new context."; break;
    case RewardEvent::ProjectPublished: a.reason = "Shipped a project others can use."; break;
    case RewardEvent::MentoredPeer: a.reason = "Mentored someone else to their own win."; break;
    case RewardEvent::Collaboration: a.reason = "Collaborated constructively."; break;
    default: a.reason.clear(); break;
  }
  return a;
}

}  // namespace dw
