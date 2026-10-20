# DEVWORLD (working title)

An open-world RPG that gamifies learning to code. Players start in a small town
with a basic computer, learn real programming by doing jobs and missions, and
grind their way from beginner to creator — with knowledge that works outside
the game.

**Stack:** C++ game core → WebAssembly (Emscripten) → browser. Zero installs.

## What's built so far

### Phase 1, step 1: the game IDE (`game-ide/`)

A real, embeddable code IDE for the game — Monaco (the actual VS Code editor)
running in the browser:

- **68 languages** registered with syntax highlighting (deduped from the
  original 70-entry list: OCaml was listed twice, T-SQL == Transact-SQL)
- **Real in-browser execution** for Python (Pyodide, newest: Python 3.14),
  JavaScript (native, newest), TypeScript (newest JS build 5.9.3 — TS 7 is
  native-only, no browser bundle), SQL (sql.js 1.15.0), Lua (Fengari, Lua 5.3),
  Ruby (ruby.wasm) and PHP (php-wasm)
- **Multi-file projects** (explorer, workspaces, tabs), a test runner, a JS
  debugger with breakpoints and variables, a terminal, git panel, docs and
  templates
- **Packages**: in-browser Python wheels via micropip, JS import maps, and an
  **approved-dependency** picker for the sandbox runtimes (only what the backend
  allows, pinned versions)
- **Revision history**: `GameIDE.saveRevision()` / `compareRevisions()` drive the
  execution worker's submissions API so a run can be diffed against the last one
- Language picker, run button, output console, per-language hello-world samples
- `window.GameIDE` API so the C++ game shell can drive it:
  `setLanguage()`, `getCode()`, `setCode()`, `runCode()`, `onRun()`,
  `runTests()`, `setBackend()`, `saveRevision()`, `compareRevisions()`

Open `game-ide/index.html` in a browser — no build step, no server.

### Execution tiers

| Tier | Meaning | Languages |
|------|---------|-----------|
| `wasm` | Runs in the browser today | Python, JavaScript, TypeScript, SQL, Lua, Ruby, PHP |
| `planned` | WASM runner staged, wiring unverified | R |
| `server` | Runs in the sandbox worker (`execution-worker/`) | C, C++, C#, Java, Go, Rust, Zig, Kotlin, Swift, Dart, Julia, Scala, Haskell, Assembly, … |
| `highlight` | Editor support only | Bash, Scratch, Q#, LabVIEW, … |

See `game-ide/languages.json` for the full registry.

### The world (`game-shell/`)

C++17 → WebAssembly: tilemaps, buildings, interiors, NPCs with schedules and
dialogue, day/night, a minimap, and procedural environmental audio (no asset
files — synthesized wind/pad/crickets/hum). Four cities are playable:

| City | Gate | Districts |
|---|---|---|
| Starter Town | open | plaza, lab, mission hall, fix-it shop, kiosk, market |
| Startup City | L6 + `m14` (First Proof) | hub, co-working, contract office, cafe |
| Systems District | L16 | data center, network ops, hardware lab, guild |
| Innovation Capital | L36 | research campus, foundry, enterprise tower, studio |

Bus stops open fast travel (level + proven mission, fare from the ledger).
Interiors are solo instances, exteriors are shared regions — the data model is
multiplayer-ready (`regionId` / `instanceType` on every map).

### Your character (`game-shell/`)

Create a name, display name, and look (skin, hair, shirt, pants), then live with
it. The profile panel shows level, title, reputation tier, playtime, achievements
("Proof of Skill"), cities visited and your load-out. Interact with NPCs,
computers, doors, boards, ATMs, mailboxes — and inspectable world objects that
tell you about the place.

**Equipment is convenience only.** Items live in slots (`computer`, `tool`,
`decor`) and can add mission slots, bonus mission XP, simulated build speed, or
comfort — never anything that changes a test verdict. The content validator
fails the build if an item grows a test-affecting effect.

Quest tracking pins an objective to the HUD; notifications keep a log of
everything the world told you. Accessibility: UI scale, text size, reduced
motion, color-blind-safe palette (nothing is signaled by colour alone), high
contrast, and fully remappable controls.

### The open world (2.3)

Four regions with different **living costs, technology markets, job focus and
specialist communities** (Starter Town free, Startup City +20% tech prices,
Systems District -15%, Innovation Capital +35%). Bus fares and shop prices read
the region profile; the world map shows the city directory side by side.

**Hidden technology dungeons** sit behind hatches on the city exteriors:
The Forgotten Repository, Derelict Data Center and the Experimental Machine
Floor. Rumours from NPCs reveal them; a mission-level gate opens them; each
holds a real programming challenge (m15-m17) and a rare, convenience-only
reward. Secrets stay hidden until discovered.

**Developer homes** are typed, decoratable spaces: workstation, decor and
trophy slots, home-lab benefits derived from what you place, achievements you
can hang on the wall, and a place to invite your team. Advanced housing is
content-extensible (private studios are more `housing.json` entries).

**Contacts, teams and companies** are Phase 1 local/single-player: six written
characters with deterministic keyword replies, unread counts, 3-person teams,
a company dashboard with focus/rank and a convenience payout preview. Every
one is marked `[PHASE2]` for server authority.

**Project portfolio and reputation** are *derived* from real mission progress,
not stored twice: evidence score = shipped projects + hidden tests passed.
Cosmetics and equipment can never change a test verdict or the evidence score.

### The education system (`game-shell/`)

The pedagogical core ([GDD] §9, design `08-education-system.md`):

- **Knowledge graph** — twelve knowledge areas split into topics
  (`content/curriculum.json`). A topic *is* the skill tag missions award
  evidence for, so curriculum, content and mastery speak one vocabulary. The
  graph carries prerequisites (a DAG), learning objectives, and
  **misconceptions with remedies and the missions that probe them**.
- **Evidence, not XP** — nine evidence kinds from `automated_test` up to
  `transfer` and `project_rubric`. Everything is weighted and time-decayed;
  nothing is stored twice (mastery is derived from the evidence log).
- **Mastery needs independence** — a skill is `mastered` only when the player
  has *independent* evidence (transfer/delayed recall) **and** working-code
  evidence. Passing the shipped tests alone scores well but can never master a
  skill. Gates (and the First Proof artifact) are the independent assessments.
- **Spaced review** — an interval ladder (1 · 3 · 7 · 16 · 35 days) grows with
  consecutive successful recalls. A finished mission can be re-run as
  **Practice** (no hints, no rewards); passing it well after the first time
  earns delayed-recall evidence, the in-game route to independently mastering a
  topic. `weakestSkills()` answers "what should I practise?". The whole model is
  pure: callers pass `nowMs`.

The game loads the graph, emits evidence on a mission pass, and the in-game
**Skills** panel renders the tree with locked/available/in-progress/mastered
topics. All of it is locally authoritative for Phase 1 (`[PHASE2]` marks what
moves server-side).

### The progression engine (`game-shell/`)

How the learning connects to the open world (design `09-progression-engine.md`):

- **Separate records, never conflated.** A derived `PlayerCapability` snapshot
  keeps the overall level, per-skill mastery, language proficiency, career
  specialization, project experience, reputation/reliability, achievements and
  company leadership as distinct fields. It is recomputed from raw records —
  nothing is stored twice — and **a level is not expertise**: a level-40 player
  with no evidence has no mastered skills.
- **An unlock catalog that explains itself.** Every opportunity (bus route,
  company eligibility, technical privilege, equipment, job tier, career) is
  data-driven (`content/unlocks.json`) and evaluated against the capability, not
  the level. Each returns *why* it was earned or *exactly what is missing*
  ("reach level 16 (you are 12)"). City access, company eligibility and
  technical privileges have **separate requirement axes**, and a technical
  privilege may only require topics from its own area — so nobody has to master
  an unrelated technology to reach a career that fits. Content validation
  enforces all of this.
- **Meaningful work only.** XP rules (`rewards.h`) reward shipping work,
  independent mastery, unseen problems, debugging recovery, delayed recall,
  transfer, publishing and mentoring — and return **zero** for repetition,
  practice and quantity of code. `completeMission()` only runs on a first full
  pass, so spam cannot earn XP structurally.
- **Careers without loss.** Career paths (`content/careers.json`) have their own
  entry requirements. Switching paths records history but never deletes
  evidence, missions or achievements, so earned competencies survive
  respecialization.
- **A dashboard that answers "why".** The **Progression & unlocks** panel shows
  every record and every unlock with its met/missing reasons.

### Tests

`game-shell/tests/` holds a native C++ + content suite, a WebAssembly build of
the same suite, and a mission verifier that proves every starter fails and every
reference solution passes. See `game-shell/tests/README.md`.

`execution-worker/` has a Docker-free `bun test` suite (request validation,
quotas, output comparison, the dependency allowlist, submission revisions and
abuse signals) run through a `FakeRunner`; real isolation runs on a Docker host.

`game-ide/tests/` has a `node --test` suite for the IDE's shared browser logic.

### Data infrastructure and services

`infra/` runs the data tier with Docker Compose — three stores, three jobs
([ARCH] §0):

| Store | Engine | Holds |
|---|---|---|
| **ClickHouse** | `clickhouse-server` | The main database: append-only events, the **double-entry ledger**, XP grants, mission attempts, assessment results, moderation signals. Balances and levels are derived. |
| **RocksDB** | Kvrocks (RocksDB engine) | Durable current state: player records, submission/revision documents, company records. |
| **Redis** | `redis:7` | Disposable: session tokens, read caches, rate-limit counters, pub/sub. |

```bash
cd infra && ./scripts/up.sh            # start + wait healthy + round-trip smoke test
./scripts/rocksdb-embedded-test.sh     # build & run a real librocksdb C++ program
```

`server/` is the game API — native C++17, no third-party libraries, HTTP/1.1 +
WebSocket over POSIX sockets — over those stores: players, username/password
sessions, the ledger, derived progression and mission grading. It never executes
player code; submissions are forwarded to the sandbox worker's `/assess`. A full
pass runs game server → worker → Python container and reads the result back from
ClickHouse and Kvrocks.

```bash
cd infra && ./scripts/up.sh
cd ../server && ./build.sh
./scripts/up.sh                         # stores + server, health-checked
./tests/run_tests.sh                    # 41 cases / 297 assertions
python3 tests/e2e.py                    # live end-to-end, 10 checks
```

The game shell connects to it from the ☁ chip / **Cloud account** panel:
username/password sign-in, live profile/level/credits from the server, and a
WebSocket for progression events. `game-server/` (Bun) is kept as a behavior
reference; it is no longer the production API.

## Roadmap

1. ✅ Game IDE (this step)
2. ✅ C++ game shell (Emscripten + WebGL): starter town, player, mission board
3. ✅ IDE ↔ game bridge: missions defined in content JSON, graded by runners
4. ✅ Levels 1–5 campaign + early Tier-2: 23 missions incl. the Level 5
   independent gate, three dungeon challenges and six Tier-2 skill missions
5. ✅ World & rendering: 4 cities, interiors, fast travel, minimap, day/night,
   procedural ambience, multiplayer-ready regions — plus native + WASM tests
6. ✅ Player identity & interaction: character creation, profile, equipment,
   quest tracking, notification log, accessibility + remappable controls
7. ✅ Server sandbox for compiled languages: `execution-worker/` (18 runtimes,
   Docker isolation, quotas, hidden-test assessment, revisioned submissions,
   approved-dependency allowlist, abuse signals)
8. ✅ Open-world layer: regional economies, hidden dungeons, decoratable homes,
   contacts/teams/companies, project portfolio — all locally authoritative and
   marked for Phase 2 server authority
9. ✅ Data tier + game server: ClickHouse (history/ledger), RocksDB/Kvrocks
   (durable state) and Redis (cache) under Docker Compose, with a native C++
   API (username/password auth, sessions, ledger, progression, grading) wired
   to the sandbox worker; the game shell's Cloud panel uses it live
10. Phase 2: multiplayer presence/chat, server-authoritative economy and
    companies, content pipeline for the 50-level ladder

## Docs

- `docs/architecture.md` — how the pieces fit together
- `docs/design/01-product-requirements.md` … `07-founder-decisions.md` — the
  product, game, world, progression, systems, testing and decision record
- `docs/design/08-education-system.md` — the knowledge graph, the evidence
  taxonomy and the mastery model behind the skill trees
- `docs/design/09-progression-engine.md` — the separate progression records, the
  unlock catalog and prerequisite engine, the meaningful-only XP rules, careers
  and respecialization
- `infra/README.md` — the data tier: how to run ClickHouse, Kvrocks (RocksDB) and
  Redis, the ClickHouse schema, and the smoke tests
- `server/README.md` — the C++ platform API: routes, auth, stores, tests and
  the front-end connection
- `game-server/README.md` — the earlier Bun API, retained as a behavior
  reference
- `execution-worker/README.md` — the sandbox worker: one-command run, layout,
  configuration
- `execution-worker/docs/api.md` — execution + assessment API
- `execution-worker/docs/threat-model.md` — isolation and what it does not cover
- `game-shell/BUILD.md` — game build instructions
- `game-shell/tests/README.md` — test suites and the mission verifier
