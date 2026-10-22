# DEVWORLD — Stage 5: The 50-level progression, rewards and world unlocks

Status: **spec locked, build in progress under the standing "do all" order**:
where a piece already exists it is checked, modified only if it falls short,
or left alone. This doc consolidates [04-progression-50-levels.md](04-progression-50-levels.md)
(all 50 levels tier by tier) and [09-progression-engine.md](09-progression-engine.md)
(the engine: separate records, unlock catalog, rewards, careers, dashboard) —
it does not replace them. Section numbers below (5.1–5.5) are 09's.

Objective: Connect the entire learning experience to the open-world game.

## 5.1 Progression engine

Implement separate records for:

- Overall player level.
- Individual skill mastery.
- Language proficiency.
- Career specialization.
- Project and professional experience.
- Reputation and reliability.
- Achievements and milestones.
- Company leadership experience.

A player's general level must not automatically grant expertise in every
technical field.

## 5.2 The 50-level structure

Use the previously defined ten five-level tiers:

| Levels | Tier | Main unlocks |
|---|---|---|
| 1–5 | Beginner | Core coding, starter town, basic equipment |
| 6–10 | Builder | Applications, Git, junior jobs |
| 11–15 | Engineer | Databases, networking, deployments |
| 16–20 | Specialist | Career paths, frameworks, studios |
| 21–25 | Systems Developer | Compilers, engines, storage systems |
| 26–30 | Inventor | AI, embedded systems, media engineering |
| 31–35 | Architect | Distributed systems, cloud, security |
| 36–40 | Innovator | Original technologies, research and leadership |
| 41–45 | Master Engineer | Advanced projects and enterprise systems |
| 46–50 | Creator | Major inventions, companies and independent engineering |

## 5.3 Unlock logic

A level can require a combination of:

- XP from meaningful activities.
- Competency assessments.
- Completed projects.
- Relevant career milestones.
- Demonstrated ability to work independently.

City access, company eligibility and technical privileges should have
separate requirements. A player should not need to master an unrelated
technology to access a career that matches their strengths.

## 5.4 Rewards

Create rewards for:

- Completing meaningful work.
- Discovering new techniques.
- Solving unfamiliar problems.
- Collaborating constructively.
- Mentoring other players.
- Publishing useful projects.
- Improving an existing system.
- Demonstrating mastery without assistance.

Avoid rewarding spam, repetitive trivial tasks or quantity of code alone.

## 5.5 Skill trees and respecialization

Support multiple career paths, cross-disciplinary branches, optional side
quests and respecialization. Players should retain earned competencies when
they change careers.

## Deliverables

Level service, skill trees, prerequisite engine, unlock catalog, achievement
system, XP rules and progression dashboard.

## Completion criterion

Every unlock is consistent with player capability, and players can see why
they earned or have not yet earned access to an opportunity.

## §6 — What is already there (checked, not rebuilt)

| Piece | State |
|---|---|
| 50 levels + tier unlocks (5.2) | Specified in 04, all 50 levels. Shell progression module implements the XP curve and gating. |
| XP rules | Server `server/src/progression.*` is deliberately identical to the shell curve, and its tests pin the equivalence. Left alone. |
| Separate records (5.1) | Specified in 09 §2. Shell has progression, mastery, careers, achievements, rewards modules. Server has XP + mastery only. |
| Unlock catalog + prerequisite engine (5.3) | Shell `unlocks.h` + mission gating (`requirementsMet`, `eligibleUnlocks`). Server-side unlock service still to come. |
| Rewards (5.4) | Shell `rewards.h`. Server-side reward rules still to come. |
| Achievements | Shell `achievements.h`. Server-side still to come. |
| Careers + respecialization (5.5) | Shell `careers.*`. Server-side still to come. |
| Progression dashboard | Specified in 09 §8. Server `progressionView()` exists; full dashboard still to come. |

Build order under "do all": (1) Stage 4 server mastery model + tests (done,
pending green suite); (2) server unlock/prerequisite engine; (3) server
achievements + rewards; (4) mission formats (Stage 4 §4.2); (5) authoring
system + skill analytics (Stage 4 §4.5); (6) AI tutor service (Stage 4 §4.4);
(7) evidence endpoints (`GET /skills/me`, `/skills/trees`) tying mastery to
progression. Each slice lands tested and pushed before the next starts.
