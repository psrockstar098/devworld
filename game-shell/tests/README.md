# Game-shell tests

Three layers, all runnable locally and in CI. Every feature lands with a test
here before it is considered done.

| Layer | Command | What it proves |
|---|---|---|
| C++ unit + content | `./tests/run_tests.sh` | Pure rules (XP, gating, world graph, ambience) and every shipped content file are valid |
| Mission solutions | `python3 tests/verify_missions.py` | Each mission's starter really fails, and a reference solution really passes the hidden tests |
| WebAssembly | `./tests/run_wasm_tests.sh` | The same C++ suite compiles and passes under Emscripten + Node |

## Files

- `test_json.cpp` — content parser.
- `test_progression.cpp` — XP curve, level cap, mission prerequisites, titles,
  ledger, objective text.
- `test_world_model.cpp` — cities, map ownership, bus routes, level/mission gating.
- `test_ambience.cpp` — day/night and per-city environmental audio levels.
- `test_equipment.cpp` — slots, equip/unequip rules, summed bonuses, mission slots.
- `test_profile.cpp` — reputation tiers, profile summary, notice log trimming.
- `test_settings.cpp` — accessibility ranges, clamping, serialization round-trip.
- `test_regions.cpp` — living cost, market bias, job focus, price rounding,
  daily upkeep.
- `test_dungeons.cpp` — discovery flags, level gates, lock reasons, done flags.
- `test_housing.cpp` — slot types, placement/clear rules, inventory return,
  derived home benefits.
- `test_social.cpp` — contacts, deterministic replies, unread counts, message
  threads, 3-person team limits.
- `test_company.cpp` — name/focus validation, ranks, derived reputation,
  convenience contract payout.
- `test_portfolio.cpp` — portfolio derived from mission state, hidden-test
  evidence score and tiers.
- `test_curriculum.cpp` — the knowledge graph: loading, prerequisites,
  topological order, topic availability, the mission reverse index, and every
  `validate()` error path (unknown area/prerequisite/mission, duplicate ids,
  missing objectives, missing remedy, cycles).
- `test_mastery.cpp` — evidence-based mastery: weighted time-decayed scores,
  decay, the independence rule (public tests alone never master a skill),
  delayed recall and the recall gap, weakest-kind reporting, spaced review,
  dedup keys and save parsing.
- `test_capability.cpp` — the derived `PlayerCapability` snapshot: reliability
  derived from outcome counters, language proficiency tiers, and the rule that a
  high level grants no mastery (separate records, objective 5.1).
- `test_unlocks.cpp` — the unlock catalog and prerequisite engine: empty
  requirements are open with a reason, level alone never satisfies a technical
  privilege, a privilege may only require its own area, city access never
  requires a skill, and milestones resolve to missions.
- `test_achievements.cpp` — idempotent awards, unmet achievements explain
  themselves, and validation rejects duplicates / missing titles / free
  achievements.
- `test_rewards.cpp` — meaningful-work-only XP: repetition, practice and code
  volume are pinned to zero; discovery, mastery, mentoring and publishing pay a
  bonus.
- `test_careers.cpp` — career entry gating, and respecialization that keeps
  evidence, completed missions and achievements.
- `test_content.cpp` — validates `content/*.json`: campaign graph, gates,
  independent assessments, NPC dialogue, shop and its convenience-only effects,
  towns, cities, interior doors, world graph ownership, inspectables, regional
  economies, dungeons (maps/missions/rewards), homes, contacts, the curriculum
  (12 areas, mission coverage, reachability), the unlock catalog (soundness,
  live references, agreement with the world graph), the achievement catalog and
  the careers.
- `verify_missions.py` — runs every mission like the IDE does (stdin → stdout,
  trimmed compare) against `solutions/`.
- `solutions/` — reference answers. They are review fixtures, **not shipped**:
  the game preloads only `content/`.

Current totals: **176 test cases / 5,839 assertions** natively and in WASM,
**50/50 missions** verified (m01-m50, including the Level 5 gate, the three
dungeon challenges and six Tier-2 skill missions), and all 65 curriculum topics
have at least one shipped mission.

## Adding a feature

1. Put the rules in a pure module (`progression`, `world_model`, `ambience`,
   `equipment`, `profile`, `settings`, `regions`, `dungeons`, `housing`,
   `social`, `company`, `portfolio`, `curriculum`, `mastery`, `capability`,
   `unlocks`, `achievements`, `rewards`, `careers`) with no SDL /
   Emscripten / I/O, so it is testable natively and as WASM.
2. Add a test file next to it.
3. If it touches content, add the invariant to `test_content.cpp` and a
   reference solution if it is a mission.
4. `./tests/run_tests.sh && ./tests/run_wasm_tests.sh`.
