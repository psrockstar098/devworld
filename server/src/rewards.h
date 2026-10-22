#pragma once
// rewards.h — XP rules for meaningful work only (objective 5.4).
//
// Server-authoritative copy (Phase 2): the server computes XP for events, so
// a client can never pay itself. The shell keeps its own copy for offline
// play. Keep the two in sync when the model changes.
//
// Rewarding spam, repetition or sheer quantity of code is explicitly out of
// bounds, so those events exist as enum members that *must* return zero XP —
// the rule lives in code, not in a comment. Meaningful events (shipping work,
// solving unfamiliar problems, recovering a system, independent mastery,
// mentoring, publishing) pay a bonus proportional to the mission's own reward
// so values stay comparable across levels.
//
// Pure: no SDL, no I/O, no clock. Design: docs/design/09-progression-engine.md.

#include <cstdint>
#include <string>

#include "missions.h"

namespace dw {

enum class RewardEvent {
  // Rewarded: the objective 5.4 list.
  MissionFirstComplete,  // completing meaningful work
  GateComplete,          // demonstrating mastery without assistance
  HiddenCaseFirst,       // solving unfamiliar problems
  ExplanationFirst,      // understanding why
  DebugRecovery,         // improving a system you broke
  DelayedRecall,         // revisiting a skill later, unaided
  TransferFirst,         // applying a skill in a new context
  ProjectPublished,      // publishing useful work
  MentoredPeer,          // mentoring others
  Collaboration,         // collaborating constructively
  // Deliberately zero: the "avoid" list.
  RepeatCompletion,      // replaying already-finished work
  PracticeRun,           // practice is its own reward
  CodeVolume,            // quantity of code is never the point
};

const char* rewardEventName(RewardEvent e);

struct XpAward {
  int64_t xp = 0;
  std::string reason;
};

// XP for one event. Bonuses are a fraction of `mission.xpReward` (with a floor
// for social events that may not have a mission). Repeat/practice/volume are 0.
XpAward xpForEvent(RewardEvent e, const MissionDef& mission);

}  // namespace dw
