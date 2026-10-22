# DEVWORLD — Education System

**Version:** 0.1 (2026-10-19)
**Reading guide:** [GDD] §9 is the learning engine's product intent; §3 is the
skill-tree UX and §12 the AI-assistance rules. This document is the *education
spec* underneath them: the knowledge graph, the evidence taxonomy and the
mastery model. It is the reference for `game-shell/src/curriculum.*`,
`game-shell/src/mastery.*` and `game-shell/content/curriculum.json`.
**Implementation status:** the model below is implemented for Phase 1 in the
game shell (native + WASM tested). Server authority, tree persistence and the
service APIs are Phase 2 ([ARCH] §2).

---

## §1 — Purpose

DEVWORLD's promise is that the knowledge works outside the game. That promise
is only as strong as the claim that a "mastered" badge means something. Two
things follow:

1. **Progress is *broad*; mastery is *deep*.** Levels ([04] ) track breadth —
   world, jobs, equipment. Mastery tracks depth: whether a player can actually
   use a concept. They are separate axes and they must not be conflated.
2. **Only evidence counts.** XP, speed, playtime, hints taken and "finished the
   tutorial" are deliberately *not* inputs to mastery. A deterministic test
   proves code works; it does not prove the player owns the idea.

## §2 — The learning loop

Every mission runs the same loop:

```
briefing → read the spec → write code → run → fail → debug → run → tests
        → public tests (examples) → hidden tests (proof) → evidence
```

The visible tests are *examples*: they show the shape of the contract. The
hidden tests probe the same rule on inputs the player never saw, so a memorised
public-test solution fails. Passing everything emits evidence, and evidence is
what moves the skill tree.

## §3 — Assessment and gates

- **Public tests** demonstrate the contract and give immediate feedback.
- **Hidden tests** are the proof: seen as PASS/FAIL only, so they cannot be
  reverse-engineered from the client.
- **Gates** (Level 5 is `m13`) are independent assessments: no hints, hidden
  tests only, no AI assistance ([GDD] §12). A gate is the only place a mission
  emits *independent* evidence, which is what mastery requires.
- **Determinism.** Same code + same tests = same verdict, always. Sandbox
  images are pinned ([ARCH] §3); a flaky verdict quarantines the mission and
  refunds the attempt — a player is never failed on an infrastructure error
  ([GDD] §9).

---

## §4 — The model

### 4.1 Knowledge graph (`curriculum.json`)

The graph has **twelve knowledge areas** — Foundations, Values & Types, Control
Flow, Iteration, Functions & Scope, Data Structures, Relational Queries,
Testing & Evidence, Debugging, Tooling & Version Control, Software Engineering,
and Systems & Performance — each split into **topics**.

A topic is the unit of mastery and *is* the skill tag:

```json
{
  "id": "loops.accumulators",
  "area": "loops",
  "title": "Accumulators",
  "difficulty": 3,
  "prerequisites": ["loops.for_iteration", "variables.numeric_math"],
  "objectives": ["Initialise an accumulator before the loop",
                 "Update the accumulator each pass"],
  "misconceptions": [{
    "id": "loops.accumulators.mc1",
    "text": "Assigning the item to the total keeps a running sum.",
    "remedy": "Assignment replaces. To accumulate you must combine: total = total + item.",
    "probes": ["m02", "m10"]
  }],
  "missions": ["m02", "m10"],
  "projects": []
}
```

- **`prerequisites`** form a DAG. `Curriculum::topologicalOrder()` gives every
  topic dependency-first; `pathTo()` gives the climb to one topic;
  `validate()` reports unknown prerequisites, self-dependencies and cycles.
- **`objectives`** are what an expert author writes down (design [04] asks for
  them per topic).
- **`misconceptions`** pair a wrong mental model with a *remedy* and the mission
  ids that **probe** it. This is the difference between "why is it wrong?" and
  "here is the answer".
- **`missions`** map shipped content to the topic; **`projects`** are planned
  practical work for topics whose missions have not shipped. A topic with
  neither is a content error.
- Mission → topic is a reverse index (`topicsForMission`). The content suite
  fails the build if **any shipped mission teaches zero topics** ([GDD] §3:
  evidence-mapping test).

`curriculum.json` is loaded by the game and parsed by the same `Curriculum`
class the test suite validates, so content and code cannot drift.

### 4.2 Evidence (`mastery.h`)

One observed act of competence. Nine kinds, ordered roughly least → most
independent:

| Kind | What it is |
|---|---|
| `automated_test` | a shipped test passes |
| `hidden_edge_case` | a hidden test passes |
| `code_quality` | maintainability review |
| `performance` | a time/memory constraint is met |
| `explanation` | the player explains *why* the behaviour happens |
| `debugging` | the player finds and repairs a defect |
| `delayed_recall` | revisits the skill later, unaided |
| `transfer` | solves an unseen problem in a new context or language |
| `project_rubric` | a larger project scored against a rubric |

Each evidence row carries `{playerId, skill, kind, score 0..1, atMs, source,
detail}`. `source` is the mission/review/project id; `atMs` is the observation
time. `evidenceKey()` is the stable identity for deduplication: the same
observation must never score twice.

### 4.3 Mastery (`mastery.h`)

For each skill:

- **`score`** — the weighted, time-decayed mean of all evidence. Weights rise
  with the independence of the kind (deterministic tests 1.0 … transfer 2.0);
  each observation is decayed by `0.5 ^ (elapsed / halfLife)`, half-life 60
  days. Recent, meaningful evidence moves the score; old evidence fades.
- **`independent`** — requires *both*:
  - **independent evidence** (`transfer` or `delayed_recall`) at or above
    `independentThreshold` (0.60), **and**
  - **working-code evidence** (`automated_test`, `hidden_edge_case`,
    `debugging`, `performance`, `code_quality`, `project_rubric`) at or above
    the same threshold.
- **`mastered`** — `independent` **and** `score >= masteryThreshold` (0.80).
- **`weakest` / `weakestScore`** — the evidence kind with the lowest best score,
  so "what should I practise?" has a concrete answer.
- **`retention`** — the decay factor of the newest evidence: how much of the
  skill is likely still there.

The consequence is deliberate: **a player who only passes the shipped tests
scores well but is never `independent` and never `mastered`.** The distinction
the education spec asks for is enforced in code, not in prose. `summarize()`,
`dueForReview()` and `weakestSkills()` are pure functions of
`(evidence, config, nowMs)`, so results are deterministic and reproducible.
Callers pass `nowMs`; the library never reads a clock.

### 4.4 Spaced review

`dueAtMs = lastEvidenceMs + reviewIntervalDays[streak]`, where `streak` counts
consecutive successful `delayed_recall` observations and the interval ladder is
1 · 3 · 7 · 16 · 35 days. `dueForReview()` returns the skills whose review is
due, soonest first; a failed recall resets the ladder.

**In-game recall (Phase 1):** a finished mission can be re-run from the board
as **Practice** — solved again from the starter, with no hints and no rewards.
If it is passed at least `recallMinAgeMs` (30 minutes) after the skill was last
exercised, it emits `delayed_recall` evidence. This is the in-game path to
independent evidence for topics a gate does not cover, and it makes the
spaced-review ladder real: a recent re-solve is scored but not "delayed", while
a genuine later revisit clears the independence bar.

---

## §5 — AI assistance

The five modes from [GDD] §12 map onto the model directly: learning, debugging
and code review produce *no* mastery evidence; independent-challenge mode
disables the AI entirely and is what gates use; professional mode requires an
"explain your change" rubric, which is `explanation` + `project_rubric`
evidence. The hard rule — the AI must not emit a complete working solution for
the current mission — is enforced server-side in Phase 2 and is the reason
transfer evidence is meaningful.

---

## §6 — Phase 1 implementation

| Piece | Where |
|---|---|
| Knowledge graph loader + DAG algorithms | `game-shell/src/curriculum.{h,cpp}` |
| Evidence + mastery model | `game-shell/src/mastery.{h,cpp}` |
| The shipped graph (20 areas, 62 topics, mapped to `m01`–`m47`) | `game-shell/content/curriculum.json` |
| Unit + content tests | `game-shell/tests/test_curriculum.cpp`, `test_mastery.cpp`, `test_content.cpp` |
| In-game skill panel | `Game::skillsJson()` + the `skilltree` panel |
| Failure coaching from authored misconceptions | `Game::completeMission` |
| Practice re-runs → delayed recall | `Game::startPractice` / `emitMissionEvidence` |
| Evidence persistence | player save (`devworld.save.v1`), evidence array |

Phase 1 is **locally authoritative** so the game is playable offline; every
piece that will move server-side is marked `[PHASE2]` in code.

## §7 — Phase 2: server authority

- **Evidence is server-issued.** The client may *request* an assessment; only
  the assessment service can append evidence. `GET /skills/me`,
  `GET /skills/trees` ([ARCH] §2).
- **Trees are content-hashed and versioned**; evidence references are hashed so
  a replayed or hand-edited submission cannot earn a node.
- **Persistence** moves to the data tier: append-only evidence in ClickHouse,
  current tree state in RocksDB/Kvrocks ([ARCH] §0).
- The client shows *why* a node is locked and never computes mastery for
  anything valuable.

## §8 — Testing

- **Model tests** (`test_mastery.cpp`): public tests alone are not mastery;
  transfer + working code is; independence respects the threshold; decay,
  weighting, weakest-kind, spaced review and dedup keys are all pinned.
- **Graph tests** (`test_curriculum.cpp`): load, dependencies, topological
  order, availability, reverse mission index, and every `validate()` error path
  (unknown area/prerequisite/mission, duplicate ids, missing objectives, missing
  remedy, cycles).
- **Content tests** (`test_content.cpp`): the shipped graph validates; **every
  shipped mission maps to ≥1 topic**; every topic is reachable; the first
  topics are open to a new player.
- **Determinism**: all library tests pass an explicit `nowMs`, so a run is
  reproducible on any machine on any day.

## §9 — Open questions

- **OPEN-1:** hidden-test *discrimination* metrics — how many memorised public
  solutions each hidden set rejects — are measured in [TEST] §9 playtests.
- **OPEN-2:** the rubric calibration for `explanation` and `project_rubric`
  evidence (who grades, and how consistently) is a Phase 2 service concern.
- **OPEN-3:** cross-tree synergy hints ([GDD] §3) need the full 50-level graph
  before they can be computed; Phase 1 ships the 23-mission slice.
