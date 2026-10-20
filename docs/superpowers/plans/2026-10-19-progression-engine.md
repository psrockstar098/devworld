# Progression Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the learning experience to the open world with a progression engine that keeps level, skill mastery, language proficiency, career specialization, project experience, reputation/reliability, achievements and company leadership as *separate* records, and gates every open-world opportunity through an explainable, data-driven unlock catalog.

**Architecture:** Four new pure modules (`capability`, `unlocks`, `achievements`, `rewards`) follow the existing flat `src/*.{h,cpp}` pure-module pattern (no SDL, no I/O, no clock — callers pass `nowMs`). Content lives in `content/*.json` and is validated by the same test suite that the game runs. A derived `PlayerCapability` snapshot — never the level alone — is the single input to unlock evaluation, so a high level can never grant a technical privilege the evidence does not support. The game shell adds thin wrappers (`Game::progressionJson()`, content loading, achievement awards) exactly as it does for `mastery`/`curriculum`.

**Tech Stack:** C++17, doctest (native g++ and Emscripten), Emscripten/WASM, vanilla JS DOM overlay (`shell/shell.js`), JSON content.

**Spec:** `docs/design/04-progression-50-levels.md` (50 levels, 10 tiers, gate invariant), `docs/design/08-education-system.md` (evidence/mastery, the model this builds on), and the objective brief 5.1–5.5 (separate records, unlock axes, rewards, respecialization, completion criterion "every unlock is consistent with player capability and players can see why").

## Global Constraints

- **Pure modules only** for new logic: no `#include <SDL2/...>`, no Emscripten headers, no `std::time`/clocks, no file I/O. Callers pass `nowMs` and parsed `JVal`.
- **Derive, never store twice.** Scores (reliability, proficiency tier, unlock status) are derived; only raw observations (evidence rows, outcome counters, chosen path, milestones) are persisted.
- **Level never grants technical expertise.** `UnlockKind::TechnicalPrivilege` unlocks must be satisfiable by the capability they name; the level alone can only open `CityAccess`.
- **Separate requirement axes.** City access, company eligibility and technical privileges must not share one gate (objective 5.3). Content validation enforces this.
- **Convenience-only rule (existing project invariant):** no reward, item effect or unlock may change a test verdict.
- **Meaningful-only XP (objective 5.4):** repeated completions, practice runs, and code *quantity* award zero XP. XP is awarded per first completion and per first-evidence event.
- **Dedup keys** use the existing `evidenceKey()` shape; achievement and unlock award is idempotent.
- **Determinism:** every library test passes an explicit `nowMs`.
- **Content is validated, not trusted:** every referenced mission/topic/area/milestone must exist or the test suite fails the build.
- Naming: `camelCase` methods, `PascalCase` types, `kConstant`, `snake_case` JSON keys. Match surrounding files.

## Review Focus

The inputs and conditions most likely to bite, most likely first. Each gets a test in the owning task.

1. **A city/company/privilege unlock whose `requires` is empty** — a reasonable person expects "unlocked", not "locked forever with no reason"; `evaluate()` must return `unlocked = true` with one `met` line.
2. **A technical privilege naming a topic from an unrelated area** — the spec forbids it; `validate()` must return an error, not silently pass.
3. **A save written before this feature (no `career`/`reliability` keys)** — must load with defaults and be playable, never crash or reset level/evidence.
4. **Award called twice for the same achievement/milestone** — must award once; `achievements` stays duplicate-free.
5. **Respecializing** — switching career path must not delete evidence, completed missions, or achievements (competencies retained, objective 5.5).
6. **An unlock referencing a mission/topic that content authors later rename** — `validate()` must name the missing id.

---

### Task 1: Capability snapshot (separate records + derivation)

**Files:**
- Modify: `game-shell/src/types.h` (add records; add two `Player` fields)
- Modify: `game-shell/src/game_init.cpp` (save ~line 300 block, load ~line 322 block)
- Create: `game-shell/src/capability.h`, `game-shell/src/capability.cpp`
- Create: `game-shell/tests/test_capability.cpp`
- Modify: `game-shell/tests/run_tests.sh`, `game-shell/tests/run_wasm_tests.sh`, `game-shell/build.sh`

**Interfaces:**
- Consumes: `Player`, `MissionDef`, `MissionProgress`, `Evidence`, `MasteryConfig`, `summarizeSkill`, `lastEvidenceAt`, `Curriculum::topicsForMission`, `evidenceKindName`.
- Produces:
  - `struct ReliabilityRecord { int passes = 0; int fails = 0; int recalls = 0; };`
  - `struct CareerRecord { std::string path; std::vector<std::string> history; std::map<std::string,std::string> milestones; };`
  - `Player::reliability` (`ReliabilityRecord`), `Player::career` (`CareerRecord`).
  - `struct LanguageProficiency { std::string language; int tier; std::string label; int missions; int tests; int independent; };`
  - `struct CareerInfo { std::string path, title, blurb; std::vector<std::string> history; };`
  - `struct PlayerCapability { int level; int64_t xp; std::vector<std::string> exercisedSkills, independentSkills, masteredSkills, completedMissions, completedGates; int independentCount; std::vector<LanguageProficiency> languages; CareerInfo career; int64_t reputation; int reliability; std::string reliabilityTier; std::vector<std::string> achievements; int companyRank; };`
  - `PlayerCapability deriveCapability(const Player&, const std::map<std::string,MissionDef>&, const Curriculum&, const MasteryConfig&, int64_t nowMs);`
  - `LanguageProficiency languageProficiency(const std::string& language, const std::map<std::string,MissionDef>&, const std::map<std::string,MissionProgress>&, const std::vector<Evidence>&, const Curriculum&);`
  - `int reliabilityScore(const ReliabilityRecord&);` `std::string reliabilityTier(int);`

- [x] **Step 1: Write the failing test**

Create `game-shell/tests/test_capability.cpp`:

```cpp
#include "doctest.h"
#include "capability.h"
#include <set>
using namespace dw;

static MissionDef mission(const std::string& id, const std::string& lang, int level,
                          int tests, bool gate = false) {
  MissionDef m; m.id = id; m.language = lang; m.level = level; m.gate = gate;
  for (int i = 0; i < tests; i++) m.tests.push_back(TestCase{"t", "", ""});
  return m;
}
static Evidence ev(const std::string& skill, EvidenceKind k, double score, int64_t at) {
  return makeEvidence("p", skill, k, score, at, "m");
}

TEST_CASE("reliability is derived from outcomes and bounded") {
  ReliabilityRecord r; r.passes = 10; r.fails = 2; r.recalls = 3;
  CHECK(reliabilityScore(r) > reliabilityScore(ReliabilityRecord{}) );
  CHECK(reliabilityScore(r) <= 100);
  CHECK(reliabilityScore(ReliabilityRecord{}) == 0);
  ReliabilityRecord bad = r; bad.fails = 1000;
  CHECK(reliabilityScore(bad) < reliabilityScore(r));
}

TEST_CASE("language proficiency grows with completed missions in that language") {
  std::map<std::string, MissionDef> ms;
  ms["a"] = mission("a", "python", 3, 2);
  ms["b"] = mission("b", "python", 5, 3);
  ms["c"] = mission("c", "sql", 4, 2);
  std::map<std::string, MissionProgress> prog;
  prog["a"].state = MissionState::Complete; prog["b"].state = MissionState::Complete;
  prog["c"].state = MissionState::Complete;
  std::vector<Evidence> evs;
  Curriculum cur;
  auto py = languageProficiency("python", ms, prog, evs, cur);
  auto sq = languageProficiency("sql", ms, prog, evs, cur);
  CHECK(py.missions == 2);
  CHECK(py.tests == 5);
  CHECK(py.tier > sq.tier);
  CHECK(languageProficiency("rust", ms, prog, evs, cur).tier == 0);
}

TEST_CASE("capability separates mastery from level and keeps history") {
  std::map<std::string, MissionDef> ms;
  ms["m13"] = mission("m13", "python", 5, 4, /*gate=*/true);
  std::map<std::string, MissionProgress> prog;
  prog["m13"].state = MissionState::Complete;
  Player p; p.levels.level = 40;  // a high level alone
  p.career.path = "systems"; p.career.history = {"python_backend", "systems"};
  MasteryConfig cfg;
  PlayerCapability cap = deriveCapability(p, ms, Curriculum{}, cfg, 1000);
  CHECK(cap.level == 40);
  CHECK(cap.masteredSkills.empty());          // level grants no mastery
  CHECK(cap.completedGates.size() == 1);
  CHECK(cap.career.history.size() == 2);      // history survives respecialization
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd game-shell && g++ -std=c++17 -I src -I tests -I tests/third_party tests/test_main.cpp tests/test_capability.cpp src/capability.cpp -o /tmp/t 2>&1 | head`
Expected: FAIL — `capability.h: No such file or directory`.

- [x] **Step 3: Implement the records and the module**

In `types.h` add the two structs above and to `Player` add:

```cpp
  ReliabilityRecord reliability;   // raw outcome counters; score is derived
  CareerRecord career;             // chosen specialization (objective 5.5)
```

In `capability.cpp`:
- `reliabilityScore`: start 0; `+ (int)round(100.0 * passes / (passes + fails))` scaled so `passes==0 && fails==0` → 0, plus `2 * recalls`, clamp 0..100. (Exact arithmetic is the implementer's; the test pins monotonicity and bounds.)
- `languageProficiency`: iterate missions with `language == language` and `progress == Complete`; sum `tests`; `tier` = 0 if none else `min(5, 1 + tests / 4)`; `independent` = count of those missions whose topics have independent evidence.
- `deriveCapability`: build `masteredSkills`/`independentSkills` from `summarizeSkill` over `curriculum.topicsForMission`; `completedGates` from gate missions; `reliability = reliabilityScore(p.reliability)`; `companyRank` from `p.company.reputation`; languages from the distinct languages in `missions`. All outputs sorted and deterministic.

Persist in `game_init.cpp`: write `,"reliability":{"passes":..,"fails":..,"recalls":..},"career":{"path":..,"history":[..],"milestones":{..}}`; on load use `j.at("reliability")` / `j.at("career")` with defaults so an old save still loads (Review Focus #3).

Add `src/capability.cpp` to `build.sh`, `tests/run_tests.sh` and `tests/run_wasm_tests.sh`; add `tests/test_capability.cpp` to both test scripts.

- [x] **Step 4: Run tests to verify they pass**

Run: `cd game-shell && ./tests/run_tests.sh 2>&1 | tail -5`
Expected: `test cases: N | N passed | 0 failed` and `== all tests passed ==`.

- [x] **Step 5: Commit**

```bash
git add game-shell/src/types.h game-shell/src/game_init.cpp \
        game-shell/src/capability.h game-shell/src/capability.cpp \
        game-shell/tests/test_capability.cpp game-shell/tests/run_tests.sh \
        game-shell/tests/run_wasm_tests.sh game-shell/build.sh
git commit -m "feat(progression): separate capability records + derived snapshot"
```

---

### Task 2: Unlock catalog + prerequisite engine

**Files:**
- Create: `game-shell/src/unlocks.h`, `game-shell/src/unlocks.cpp`
- Create: `game-shell/content/unlocks.json`
- Create: `game-shell/tests/test_unlocks.cpp`
- Modify: `game-shell/tests/test_content.cpp` (validate the shipped catalog)
- Modify: `game-shell/tests/run_tests.sh`, `tests/run_wasm_tests.sh`, `build.sh`

**Interfaces:**
- Consumes: `PlayerCapability` (Task 1), `JVal`, `json_escape` (existing `json.h`).
- Produces: the `UnlockDef` / `UnlockRequirement` / `UnlockStatus` / `UnlockCatalog` API in the Architecture note, plus `unlockKindName`/`unlockKindFromString`. `evaluate()` returns `met`/`missing` human strings; `evaluateAll()` returns one `UnlockStatus` per definition, sorted by id. `validate()` returns content errors.

- [x] **Step 1: Write the failing test**

Create `game-shell/tests/test_unlocks.cpp` with cases:

```cpp
TEST_CASE("an unlock with no requirements is open with a reason") {
  UnlockDef d; d.id = "x"; d.kind = UnlockKind::CityAccess;
  PlayerCapability cap;            // fresh player
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK(s.unlocked);
  CHECK(s.met.size() >= 1);
  CHECK(s.missing.empty());
}

TEST_CASE("level alone does not satisfy a technical privilege") {
  UnlockDef d; d.id = "tp"; d.kind = UnlockKind::TechnicalPrivilege; d.area = "data";
  d.requires.independentSkills = {"data.mapping_keys"};
  PlayerCapability cap; cap.level = 50;   // high level only
  UnlockStatus s = UnlockCatalog{}.evaluate(d, cap);
  CHECK_FALSE(s.unlocked);
  CHECK(s.missing.size() == 1);
}

TEST_CASE("a technical privilege may only require topics in its own area") {
  UnlockCatalog c; c.load(parseJson(R"({"unlocks":[
    {"id":"bad","kind":"technical_privilege","area":"data",
     "requires":{"business":["control_flow.boolean_logic"]}}]})"));
  CHECK_FALSE(c.validate().empty());
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd game-shell && g++ -std=c++17 -I src -I tests -I tests/third_party tests/test_main.cpp tests/test_unlocks.cpp src/unlocks.cpp -o /tmp/t 2>&1 | head`
Expected: FAIL — `unlocks.h: No such file or directory`.

- [x] **Step 3: Implement and author content**

Implement `unlocks.cpp` per the API. Requirement axes (each optional, all present axes must hold):
`minLevel`, `missions`, `gates`, `skills` (mastered), `independentSkills`, `minIndependent`, `milestone`, `minReliability`, `company`. `met`/`missing` lines are human sentences built from the capability, e.g. `"Level 5 reached (you are 5)"` and `"Independent skill required: Mapping keys"` (look the topic title up via `Curriculum` passed into `evaluateAll`, or store the title in the requirement — simplest: `evaluate()` takes an optional `const Curriculum*` for pretty names).

Author `content/unlocks.json` covering the shipped slice with **separate axes per kind**:
- `city_access: starter_town→startup_city` — level + `m13` only (mirror `world.json`).
- `company_eligibility: found_studio` — level + a milestone/gate + reliability, no specific tech.
- `technical_privilege`: e.g. `terminal.git` (`area: tooling`, requires `tooling.git_history` independent), `db.inspector` (`area: data`, requires `data.mapping_keys`), each naming its own area only.
- `equipment`, `job_tier` examples for the shipped towns.

Add to `test_content.cpp`: load `content/unlocks.json`, `CHECK(catalog.validate().empty())`, and `CHECK(every unlock references an existing mission/topic/milestone)`.

- [x] **Step 4: Run tests to verify they pass**

Run: `cd game-shell && ./tests/run_tests.sh 2>&1 | tail -5`
Expected: all pass.

- [x] **Step 5: Commit**

```bash
git add game-shell/src/unlocks.h game-shell/src/unlocks.cpp \
        game-shell/content/unlocks.json game-shell/tests/test_unlocks.cpp \
        game-shell/tests/test_content.cpp game-shell/tests/run_tests.sh \
        game-shell/tests/run_wasm_tests.sh game-shell/build.sh
git commit -m "feat(progression): unlock catalog + explainable prerequisite engine"
```

---

### Task 3: Wire the unlock catalog into the game

**Files:**
- Modify: `game-shell/src/game.h` (member `UnlockCatalog unlocks_;`)
- Modify: `game-shell/src/game_init.cpp` (`loadContent()` — load `content/unlocks.json`)
- Modify: `game-shell/src/game_logic.cpp` (`travelJson`/`travelTo` reasons, `companyJson`/`foundCompany` eligibility)
- Test: `game-shell/tests/test_content.cpp`

**Interfaces:**
- Consumes: `UnlockCatalog`, `deriveCapability`, existing `WorldGraph::offers`.
- Produces: the catalog is authoritative for the *reason* text and the company gate; `WorldGraph` still owns routes/fares/maps.

- [x] **Step 1: Write the failing test**

In `test_content.cpp`, add a consistency case: for each shipped city, the `city_access` unlock's `minLevel`/mission equals the matching `world.json` `minLevel`/`requiresMission` — pins the two sources together so they cannot drift.

- [x] **Step 2: Run test to verify it fails**

Run: `cd game-shell && ./tests/run_tests.sh 2>&1 | grep -i "city\|unlock" | head`
Expected: FAIL (no `city_access` unlocks yet or mismatched).

- [x] **Step 3: Implement the wiring**

Load the catalog in `loadContent()` next to `curriculum_.load(...)`. In `travelJson`, prefix each locked offer's `reason` with the catalog's `missing` lines when a matching `city_access` unlock exists. In `foundCompany`, consult the `company_eligibility` unlock and refuse with the `missing` lines instead of a bare error. Keep all existing behaviour when no catalog entry matches (old saves / partial content).

- [x] **Step 4: Run tests + build**

Run: `cd game-shell && ./tests/run_tests.sh 2>&1 | tail -3`
Run: `cd game-shell && source ~/emsdk/emsdk_env.sh && ./build.sh 2>&1 | tail -3`
Expected: tests pass; `== done ==`.

- [x] **Step 5: Commit**

```bash
git add game-shell/src/game.h game-shell/src/game_init.cpp game-shell/src/game_logic.cpp \
        game-shell/tests/test_content.cpp
git commit -m "feat(progression): drive city + company gates from the unlock catalog"
```

---

### Task 4: Achievement system

**Files:**
- Create: `game-shell/src/achievements.h`, `game-shell/src/achievements.cpp`
- Create: `game-shell/content/achievements.json`
- Create: `game-shell/tests/test_achievements.cpp`
- Modify: `game-shell/src/game_init.cpp` (load; award on load reconciliation), `game-shell/src/game_logic.cpp` (award points), `game-shell/tests/test_content.cpp`

**Interfaces:**
- Consumes: `PlayerCapability`, `JVal`.
- Produces:
  - `struct AchievementDef { std::string id, title, desc, trophy; /* rule fields */ UnlockRequirement requires; bool hidden; };`
  - `class AchievementCatalog { void load(const JVal&); const std::vector<AchievementDef>& all() const; const AchievementDef* def(const std::string&) const; std::vector<std::string> newlyEarned(const PlayerCapability&, const std::vector<std::string>& held) const; std::vector<std::string> validate() const; };`
  - `void awardAchievements(Player&, const AchievementCatalog&, const PlayerCapability&, std::string* firstNew = nullptr);` (idempotent; append unique ids)

- [x] **Step 1: Write the failing test**

```cpp
TEST_CASE("achievements award once and never duplicate") {
  AchievementCatalog c; c.load(parseJson(R"({"achievements":[
    {"id":"first_proof","title":"First Proof","requires":{"gates":["m13"]}}]})"));
  Player p; PlayerCapability cap; cap.completedGates = {"m13"};
  awardAchievements(p, c, cap);
  awardAchievements(p, c, cap);
  CHECK(p.achievements.size() == 1);
  CHECK(p.achievements[0] == "first_proof");
}
```

- [x] **Step 2: Run test to verify it fails** — same compile-fail pattern as Task 2.

- [x] **Step 3: Implement + author content**

Reuse `UnlockRequirement` for achievement rules (DRY). Migrate the ad-hoc strings (`visited_<city>`, `level_<n>`, dungeon clears) into `content/achievements.json`; keep the in-game `push_back` call sites but route them through `awardAchievements` so ids stay unique. Add `validate()` to `test_content.cpp`.

- [x] **Step 4: Run tests to verify they pass.**

- [x] **Step 5: Commit** — `git commit -m "feat(progression): data-driven achievement system"`

---

### Task 5: Reward rules (meaningful-only XP)

**Files:**
- Create: `game-shell/src/rewards.h`, `game-shell/src/rewards.cpp`
- Create: `game-shell/tests/test_rewards.cpp`
- Modify: `game-shell/src/game_logic.cpp` (`completeMission` / `completePractice` / discovery sites)
- Modify: `game-shell/build.sh`, `tests/run_tests.sh`, `tests/run_wasm_tests.sh`

**Interfaces:**
- Produces:
  - `enum class RewardEvent { MissionFirstComplete, GateComplete, HiddenCaseFirst, ExplanationFirst, DebugRecovery, DelayedRecall, TransferFirst, ProjectPublished, MentoredPeer, RepeatCompletion, PracticeRun, CodeVolume };`
  - `struct XpAward { int64_t xp; std::string reason; };`
  - `XpAward xpForEvent(RewardEvent, const MissionDef&, const MasteryConfig&);`
  - `std::vector<RewardEvent> rewardedEvents(...)` only if needed — otherwise `xpForEvent` suffices.

- [x] **Step 1: Write the failing test**

```cpp
TEST_CASE("repetition and code volume earn nothing") {
  MissionDef m; m.xpReward = 250; m.level = 4;
  CHECK(xpForEvent(RewardEvent::MissionFirstComplete, m, {}).xp == 250);
  CHECK(xpForEvent(RewardEvent::RepeatCompletion, m, {}).xp == 0);
  CHECK(xpForEvent(RewardEvent::PracticeRun, m, {}).xp == 0);
  CHECK(xpForEvent(RewardEvent::CodeVolume, m, {}).xp == 0);
}
TEST_CASE("meaningful discovery and mastery pay a bonus") {
  MissionDef m; m.xpReward = 250;
  CHECK(xpForEvent(RewardEvent::TransferFirst, m, {}).xp > 0);
  CHECK(xpForEvent(RewardEvent::MentoredPeer, m, {}).xp > 0);
}
```

- [x] **Step 2: Run test to verify it fails.**
- [x] **Step 3: Implement** — a pure switch; comment each case against the 5.4 list. Route `Game::completeMission` through it; practice and repeats pass the zero events.
- [x] **Step 4: Run tests to verify they pass.**
- [x] **Step 5: Commit** — `git commit -m "feat(progression): meaningful-work-only XP rules"`

---

### Task 6: Careers and respecialization

**Files:**
- Create: `game-shell/content/careers.json`
- Create: `game-shell/src/careers.h`, `game-shell/src/careers.cpp`
- Create: `game-shell/tests/test_careers.cpp`
- Modify: `game-shell/src/game_logic.cpp` (uiEvent `choose_career`), `game-shell/src/game_init.cpp` (load), `game-shell/src/game.h`
- Modify: `game-shell/tests/test_content.cpp`

**Interfaces:**
- Produces:
  - `struct CareerDef { std::string id, title, language, blurb; UnlockRequirement entry; };`
  - `class Careers { void load(const JVal&); const std::vector<CareerDef>& all() const; const CareerDef* def(const std::string&) const; std::vector<UnlockStatus> eligibility(const PlayerCapability&, const UnlockCatalog&) const; bool choose(Player&, const CareerDef&, std::string* reason = nullptr) const; std::vector<std::string> validate() const; };`
  - `choose()` appends to `player.career.history` (dedup consecutive), sets `player.career.path`, records milestone `career_<id>`; **never** touches evidence/missions/achievements.

- [x] **Step 1: Write the failing test**

```cpp
TEST_CASE("respecializing keeps earned competencies") {
  Careers cs; cs.load(parseJson(R"({"paths":[
    {"id":"python_backend","title":"Backend","language":"python","entry":{}},
    {"id":"systems","title":"Systems","language":"c","entry":{}}]})"));
  Player p; p.evidence.push_back(makeEvidence("p","loops.for_iteration",EvidenceKind::Transfer,1,1,"m"));
  p.missions["m01"].state = MissionState::Complete;
  std::string why;
  CHECK(cs.choose(p, *cs.def("python_backend"), &why));
  CHECK(cs.choose(p, *cs.def("systems"), &why));
  CHECK(p.career.path == "systems");
  CHECK(p.career.history.size() == 2);
  CHECK(p.evidence.size() == 1);          // retained
  CHECK(p.missions.at("m01").state == MissionState::Complete);
}
```

- [x] **Step 2: Run test to verify it fails.**
- [x] **Step 3: Implement + author content** — 4–6 paths (Backend Python, Data & SQL, Systems C, Web, Tools), each with `entry` requirements (separate axes) and a `language`. Open paths at the level their `entry.minLevel` says in content (Phase 1 content uses 5; note the Phase-1 shortcut in docs). `choose()` refuses when `entry` unmet, returning the missing lines.
- [x] **Step 4: Run tests to verify they pass.**
- [x] **Step 5: Commit** — `git commit -m "feat(progression): career paths with retention on respecialization"`

---

### Task 7: Progression dashboard (see why)

**Files:**
- Modify: `game-shell/src/game_logic.cpp` (`progressionJson()`, `panelJson("progression")`), `game-shell/src/game.h`
- Modify: `game-shell/shell/shell.js` (new `progression` overlay + profile-menu entry + `choose_career` action routing)
- Test: manual + `node --check shell/shell.js`

**Interfaces:**
- Consumes: `deriveCapability`, `UnlockCatalog::evaluateAll`, `Careers::eligibility`, `AchievementCatalog`.
- Produces: `std::string Game::progressionJson()` returning levels, per-language proficiency, career (current + history + eligibility with reasons), reliability, achievements, and the unlock list with `{id,title,kind,unlocked,met[],missing[]}`. The panel renders a row per unlock with its reasons.

- [x] **Step 1: Implement `progressionJson()`** and register the panel in `panelJson`; add the `progression` overlay renderer and a `Profile → Progression dashboard` menu button in `shell.js`; route `choose_career` through `uiEvent`.
- [x] **Step 2: Verify the shell parses**

Run: `cd game-shell && node --check shell/shell.js && echo js-ok`
Expected: `js-ok`.

- [x] **Step 3: Build and serve**

Run: `cd game-shell && source ~/emsdk/emsdk_env.sh && ./build.sh 2>&1 | tail -3`
Then open `http://localhost:8000/` (server already running) → Profile → Progression dashboard. Confirm every locked unlock shows a reason line.

- [x] **Step 4: Commit** — `git commit -m "feat(progression): dashboard that explains every unlock"`

---

### Task 8: Documentation + full verification

**Files:**
- Create: `docs/design/09-progression-engine.md`
- Modify: `README.md`, `docs/architecture.md`, `game-shell/tests/README.md`, `docs/design/04-progression-50-levels.md`

- [x] **Step 1: Write `docs/design/09-progression-engine.md`** covering 5.1 (the records), 5.2 (tiers — reference 04), 5.3 (unlock axes), 5.4 (reward rules), 5.5 (respecialization), the completion criterion, and the Phase-1 scope (which unlocks ship, what is `[PHASE2]`).
- [x] **Step 2: Update the counts and tables** in README/architecture/tests-README; add a Phase-1 build-status note to `04`.
- [x] **Step 3: Full verification**

Run: `cd game-shell && ./tests/run_tests.sh 2>&1 | tail -5`
Run: `cd game-shell && ./tests/run_wasm_tests.sh 2>&1 | tail -4`
Run: `cd game-shell && source ~/emsdk/emsdk_env.sh && ./build.sh 2>&1 | tail -3`
Expected: native + WASM totals pass; mission verifier `0 mission(s) failed`; build `== done ==`.

- [x] **Step 4: Commit** — `git commit -m "docs(progression): progression engine + dashboard spec"`

---

## Self-Review

**Spec coverage:** 5.1 → Task 1 (records) + Tasks 2/4/6 (catalogs) + Task 7 (dashboard). 5.2 → already implemented (`titleForLevel`, 10 tiers) + documented in Task 8. 5.3 → Tasks 2–3 (separate axes, explainability). 5.4 → Task 5. 5.5 → Task 6. Deliverables (level service, skill trees, prerequisite engine, unlock catalog, achievement system, XP rules, dashboard) → Tasks 1–7. Completion criterion (consistent with capability + explainable) → Tasks 2, 3, 7.

**Step scan:** each step names one file and one checkable result. Bodies appear only where the test does not determine them (reliability arithmetic, reason strings).

**Type consistency:** `UnlockRequirement` is defined once (Task 2) and reused by achievements (Task 4) and careers (Task 6); `PlayerCapability` is defined once (Task 1) and consumed everywhere. `evaluate`/`evaluateAll` names are stable across Tasks 2, 3 and 7.

**Review Focus:** (1) empty-requirements → Task 2 Step 1. (2) unrelated-area privilege → Task 2 Step 1 + Step 3 content validation. (3) old save → Task 1 Step 3. (4) double award → Task 4 Step 1. (5) respecialization → Task 6 Step 1. (6) renamed mission/topic → Task 2 Step 3 `validate()`.

**Proportion:** the plan is larger than the spec because it fixes names, signatures and content shape the spec leaves open; code bodies are omitted except where an algorithm or exact copy is required.
