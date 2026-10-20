# DEVWORLD — Progression Engine

**Version:** 0.1 (2026-10-19)
**Reading guide:** [04] is the 50-level / 10-tier progression spec; [08] is the
education spec (knowledge graph, evidence, mastery). This document is the
*engine underneath them*: the separate records a developer earns, the
data-driven catalog of open-world opportunities and the rules that decide who
qualifies. It is the reference for `game-shell/src/capability.*`,
`game-shell/src/unlocks.*`, `game-shell/src/achievements.*`,
`game-shell/src/rewards.*`, `game-shell/src/careers.*` and the content files
`game-shell/content/unlocks.json`, `achievements.json`, `careers.json`.
**Implementation status:** implemented for Phase 1 in the game shell (native +
WASM tested). Server authority and the level/unlock service APIs are Phase 2
([ARCH] §2).

---

## §1 — Purpose

Two claims hold the progression system together.

1. **Progress is broad; mastery is deep — and a level is not expertise.** The
   overall level tracks breadth (XP from many kinds of work). Skill mastery
   tracks depth ([08]). A player at level 40 with no evidence has *no* mastered
   skills, and no level can unlock a technical privilege. The engine enforces
   this mechanically: **every** unlock is evaluated against a derived
   [`PlayerCapability`], never against the raw level.
2. **Every opportunity explains itself.** For each unlock the player sees either
   *why* it was earned or *exactly what is missing*. "You cannot enter" is a
   bug; "reach level 16 (you are 12)" is the product.

The five required efforts map to concrete records, catalogs and rules below.

---

## §2 — The separate records (5.1)

`PlayerCapability` (`capability.h`) is a pure, derived snapshot. Nothing in it
is stored twice: it is recomputed from the player's raw records on demand.

| Record | Type | Source of truth |
|---|---|---|
| Overall player level | `PlayerCapability::level`, `::xp` | `LevelState` (XP from [04]) |
| Individual skill mastery | `::exercisedSkills`, `::independentSkills`, `::masteredSkills` | `Evidence` via `summarize` ([08] §4.2) |
| Language proficiency | `::languages[]` (`tier` 0–5, label) | completed missions grouped by `MissionDef.language` |
| Career specialization | `::career` (path + history) | `CareerRecord` (5.5) |
| Project / professional experience | `::completedMissions`, `::completedGates` | `MissionProgress` |
| Reputation & reliability | `::reputation`, `::reliability`, `::reliabilityTier` | `Player::reputation`; `ReliabilityRecord` counters |
| Achievements & milestones | `::achievements`; catalog milestones | `Player::achievements`; mission completion via the catalog |
| Company leadership | `::companyRank` | `CompanyState` |

**Reliability is derived, not stored as a score.** `ReliabilityRecord` holds
only raw outcome counters (`passes`, `fails`, `recalls`), incremented at the
points where an outcome happens. `reliabilityScore()` turns them into 0–100
(completion ratio plus a small consistency term for successful recalls) and
`reliabilityTier()` names the band (`unproven` … `flawless`). A fresh player is
`unproven`, never "trusted by default".

---

## §3 — Levels and tiers (5.2)

The 50-level, 10-tier ladder (Beginner → Creator) and its unlock table are
specified in [04] and unchanged here. This engine *consumes* level as one
requirement axis among several; it never lets a level stand in for a competency.
`titleForLevel()` names the tier.

---

## §4 — The unlock catalog and prerequisite engine (5.3)

Every open-world opportunity is one data-driven `UnlockDef` in
`content/unlocks.json`. Six kinds are defined:

| Kind | What it opens | Its characteristic axes |
|---|---|---|
| `city_access` | a bus route / district | level + missions/milestones (breadth) |
| `company_eligibility` | founding or upgrading a company | gates, reliability, company reputation (leadership) |
| `technical_privilege` | a tool tied to one field | **skills/independent skills in exactly one area** |
| `equipment` | a purchasable item/upgrade | level, missions (convenience only) |
| `job_tier` | a class of contract | level + the relevant skill |
| `career` | a selectable career path | see §6 |

**Requirement axes** (`UnlockRequirement`), all optional, all must hold:
`minLevel`, `missions`, `gates`, `milestones`, `skills` (mastered),
`independentSkills`, `minIndependent`, `minReliability`, `requiresCompany`,
`minCompanyRep`.

### §4.1 — Separate requirements, on purpose

City access, company eligibility and technical privileges must not share a
single gate. `UnlockCatalog::validate()` enforces the separation at build time:

- A **technical privilege** must name its `area` *and* require a competency
  (a skill, gate or milestone) — never level or XP alone.
- A technical privilege may only require topics **from its own area**. Topic ids
  are `"<area>.<name>"`, so this is checked from the id itself. This is the
  mechanical form of the rule that *a player never has to master an unrelated
  technology to reach a career that matches their strengths*.
- **City access** may not require a specific skill — it is breadth.
- Referenced missions/topics/milestones must exist.

### §4.2 — Explainability

`evaluate()` returns an `UnlockStatus { unlocked, met[], missing[] }` with human
lines built from the capability, e.g. `"Reach level 16 (you are 12)"`,
`"Prove independent ability in Mapping keys"`, `"Earn the milestone: First
Proof"`. An unlock with no requirements is open and reports `"Open to
everyone"`. `metRequirements`/`unmetRequirements` are shared with the
achievement and career systems, so all three explain themselves identically.

### §4.3 — Milestones

Named milestones (`content/unlocks.json` → `milestones`) resolve to a defining
mission and are earned by completing it. This keeps the "relevant career
milestone" axis data-driven without duplicating mission state: milestone
earnings are *derived* from `completedMissions`.

### §4.4 — One explanation, one place

City gates appear in two content files (`world.json` for routes,
`unlocks.json` for the explanation). A content test pins them together: for
every city, the `city.<id>` unlock's level and mission must equal `world.json`.
The bus stop and the dashboard therefore cannot disagree. The company gate is
driven directly from the catalog in `foundCompany()`.

---

## §5 — Rewards: meaningful work only (5.4)

`rewards.h` names every event the game can reward. The "avoid" list is not a
comment — those events are enum members that **must** return zero XP:

- `RepeatCompletion` — replaying finished work: 0.
- `PracticeRun` — practice is its own reward: 0.
- `CodeVolume` — quantity of code is never the point: 0.

Rewarded events pay the mission's own reward (`MissionFirstComplete`) or a
fraction of it, so values stay comparable across levels: `GateComplete` (+50%,
demonstrated mastery without assistance), `TransferFirst` (+25%, applying a
skill in a new context), `ProjectPublished` (+20%), `DelayedRecall` (+15%,
revisiting a skill later unaided), `HiddenCaseFirst` (+10%, solving unfamiliar
problems), `DebugRecovery` (+10%, improving a system you broke),
`ExplanationFirst` (+5%). `MentoredPeer` and `Collaboration` carry a floor so
constructive social work pays even without a mission reward.

Because `completeMission()` runs only on a *first* full pass (re-runs go through
the practice path), spam and repetition structurally cannot award XP.

---

## §6 — Careers and respecialization (5.5)

Career paths live in `content/careers.json`. Each has an `entry`
(`UnlockRequirement`) so different paths match different strengths — a systems
path can require systems work while a data path requires data work.

`Careers::choose()` refuses when entry requirements are unmet and writes the
reason. On success it sets `career.path` and appends to `career.history`
(deduplicating consecutive picks). It **never** deletes evidence, completed
missions or achievements, and `career.history` records every path ever held:
that is respecialization without losing earned competencies. Cross-disciplinary
branches fall out for free, because a player can qualify for a second path
using skills they built on the first.

*Phase 1 note:* the shipped paths open at the levels their content entry names
(currently level 5–10) rather than [04]'s illustrative level 16, so the mechanic
is reachable in the Phase 1 content slice.

---

## §7 — Achievements

`content/achievements.json` defines achievements with the same requirement axes,
so each has a concrete condition and a reason when unmet
(`awardAchievements` / `AchievementCatalog::progress`). Awards are idempotent:
an id is appended only if absent, so re-awarding is a no-op. Legacy ad-hoc ids
(`visited_<city>`, `level_<n>`, dungeon ids) remain valid; the catalog adds
titles and descriptions for the ones it knows, and the dashboard shows catalog
achievements with an earned/locked state. A content test forbids "free"
achievements (no requirements = noise).

---

## §8 — Progression dashboard

`Game::progressionJson()` (panel `progression`, Profile → *Progression &
unlocks*) renders every separate record and the whole unlock catalog. Each
locked unlock shows its `missing` lines and each open one shows its `met` lines,
so the player can always answer *why*. Careers show eligibility and reasons with
Choose/Switch buttons; choosing refreshes the panel via the `choose_career`
event.

---

## §9 — Completion criterion and verification

**Criterion:** every unlock is consistent with player capability, and the player
can see why they earned or have not yet earned access.

- *Consistent with capability:* unlocks evaluate a derived `PlayerCapability`;
  a technical privilege cannot be satisfied by level alone, and cannot require
  an unrelated area (both enforced by `validate()` and covered by
  `test_unlocks.cpp` + the content tests).
- *See why:* `evaluate()` returns `met`/`missing`; the dashboard renders them;
  the bus stop and company gate reuse the same strings.

Tests: `test_capability.cpp`, `test_unlocks.cpp`, `test_achievements.cpp`,
`test_rewards.cpp`, `test_careers.cpp`, plus content invariants in
`test_content.cpp` (catalog soundness, live references, world/unlock agreement,
career entries present). See `game-shell/tests/README.md` for the totals.

---

## §10 — Phase boundaries

Phase 1 (now): records, catalogs, evaluation, rewards, careers, dashboard — all
locally authoritative and fully unit-tested.

Phase 2 (`[PHASE2]`): server-authoritative level/unlock service, signed
capability snapshots, catalog served from the service, and authoritative
achievement grants.
