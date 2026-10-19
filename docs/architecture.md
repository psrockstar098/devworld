# Architecture — DEVWORLD

## The shape

```
┌─────────────────────────────────────────────────────────────┐
│  Browser — the Game IDE (in-game computer terminal)         │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ IDE chrome (Codient-style): activity bar, explorer,   │   │
│  │ tabs, search, xterm terminal, problems, git panel,    │   │
│  │ test runner, debugger (JS breakpoints + variables),   │   │
│  │ docs cheat-sheets, packages, templates, workspaces    │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────┐      ┌────────────────────────────────┐    │
│  │ C++ game     │◄────►│ window.GameIDE API             │    │
│  │ shell (WASM) │ API  │ setLanguage/getCode/setCode/   │    │
│  │ via          │      │ runCode/onRun/runTests/        │    │
│  │ Emscripten   │      │ setBackend/openFile/listFiles  │    │
│  └──────────────┘      └────────────────────────────────┘    │
│                                                             │
│  World (game-shell): Starter Town + Startup City + Systems  │
│  District + Innovation Capital; interiors are solo          │
│  instances, exteriors are shared regions; bus-stop fast     │
│  travel gated by level + a proven mission (world_model).    │
│  Regional economies (rent, market bias, jobs, communities), │
│  hidden dungeon hatches, decoratable homes and a derived    │
│  portfolio/company layer sit on top.                        │
│  Pure rules (progression, world_model, ambience, regions,   │
│  dungeons, housing, social, company, portfolio) are shared  │
│  with the native + WASM test suite.                         │
│                                                             │
│  Execution tiers:                                           │
│   • wasm    — in-browser: Python, JS, TS, SQL, Lua, Ruby,   │
│               PHP (lazy-loaded WASM runtimes)               │
│   • server  — sandbox backend (see below): C, C++, C#,      │
│               Java, Go, Rust, Zig, Kotlin, Swift, Dart,      │
│               Julia, Scala, Haskell, Assembly               │
│   • planned — R (runner code staged, unverified in CI env)  │
│   • highlight — editor-only (20 langs)                      │
└─────────────────────────────────────────────────────────────┘
                            │  (player-configured URL + API key)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  execution-worker/  (Bun + TypeScript, Docker)               │
│  POST /execute      {runtime, files[], stdin?, timeout?,     │
│                      dependencies?, submissionId?}           │
│  POST /assess       {runtime, files[], tests[] (hidden flag)}│
│  GET  /runtimes     (18-entry registry)                      │
│  GET  /dependencies (approved-package allowlist)             │
│  POST /submissions  + GET revisions / compare (3.4)          │
│  GET  /moderation/signals (advisory abuse feed)              │
│                                                             │
│  Per run: fresh container, --network none, --read-only,     │
│  --cap-drop ALL, no-new-privileges, pids/memory/cpu limits, │
│  wall-clock timeout, 256KB output cap, 5MB artifact cap.     │
│  API-key auth, per-key quotas (60/hr), concurrency cap (4), │
│  approved-dependency allowlist (no network, pinned versions).│
│  Hidden tests: response carries only pass/fail — never      │
│  stdin/expected/actual. See docs/threat-model.md.           │
│                                                             │
│  Deploy: `docker compose up` on any Docker host. The game   │
│  frontend points at it via Settings → backend URL + API key. │
└─────────────────────────────────────────────────────────────┘
```

## Why this split

- **Game shell in C++ → WASM.** Near-native speed, one codebase, runs
  anywhere. Emscripten gives us WebGL rendering and SDL-style input.
- **Pure rules in separate modules.** `progression.*`, `world_model.*`,
  `ambience.*`, `equipment.*`, `profile.*`, `settings.*`, `regions.*`,
  `dungeons.*`, `housing.*`, `social.*`, `company.*` and `portfolio.*` have no
  SDL, Emscripten or I/O, so the same code runs in the game and in the native +
  WASM test suites (`game-shell/tests/`).
- **IDE stays web-native (Monaco).** Rebuilding VS Code's editor in C++
  would be madness — Monaco *is* VS Code's editor. The game shell drives
  it through the `window.GameIDE` JS bridge (Emscripten `EM_JS` /
  `emscripten::val`).
- **Execution in tiers.** Interpreted languages run free in the browser
  via WASM runtimes (lazy-loaded so first paint stays fast). Compiled
  languages go to the sandbox worker: isolated containers, resource
  limits, no network — player code never runs on the game server itself.

## Mission flow (the core loop)

1. Player walks to the mission board in the starter town (C++ side).
2. Game calls `GameIDE.setLanguage('python')` + `setCode(starter)`.
3. Player writes code, hits Run (or the game triggers `runCode()`).
4. `onRun` callback reports pass/fail back to the C++ shell.
5. `GameIDE.runTests()` grades against public + hidden tests —
   hidden results reveal only pass/fail ("Proof of Skill").
6. Rewards: XP, in-game money, unlocks — handled by the game shell.

With the backend running (`infra/` + `game-server/` + `execution-worker/`), the
same submission is graded authoritatively: the client posts to `game-server`,
which forwards to the worker's `/assess` (hidden tests never leave the server),
then writes the attempt, XP and a balanced ledger transaction to ClickHouse and
the updated profile to Kvrocks. The local shell grading is the offline preview;
the worker is the source of truth.

## Safe code execution

- Browser tier: WASM sandboxes (Pyodide, sql.js, Fengari, ruby.wasm,
  php-wasm) — no server, no cost, no escape to the player's machine
  beyond the page.
- Server tier: one-shot Docker containers per run (flags + limits above),
  API key + quotas + concurrency caps, and an approved-dependency allowlist
  (no network; pinned versions only). Assessment infrastructure is separated
  from production: the worker holds no player accounts and hidden tests live
  in the game server. Player projects are kept as **revisioned submissions**
  so a run can be compared to the last one. Threat model lives in
  `execution-worker/docs/threat-model.md` — including what it does NOT
  cover (kernel escapes need gVisor/Firecracker; docker.sock = host root,
  so run the worker on a dedicated host).

## Runtime services and the data tier

Two Bun services back the game, plus the stores they share (all under `infra/`):

- **`execution-worker/` (§3)** — runs untrusted code in one-shot containers. The
  only service that executes player code. Grading outcomes and moderation
  signals go to ClickHouse (best-effort, never blocking a grade); submission
  documents persist to RocksDB (Kvrocks) behind the same `SubmissionStore`
  interface.
- **`game-server/` (§1/§2/§5/§6)** — players, sessions, the ledger, progression
  and mission grading. It forwards submissions to the worker's `/assess` and
  never executes code itself.

Stores (see `infra/README.md`):

| Store | Role |
|---|---|
| **ClickHouse** | History: events, the double-entry ledger, XP, attempts, assessments, moderation. Append-only. |
| **RocksDB (Kvrocks)** | Durable current state: player records, submission/revision documents. |
| **Redis** | Disposable: session tokens, read caches, rate limits, pub/sub. |

`cd infra && ./scripts/up.sh` starts and proves the stores. The request path is
client → `game-server` → `execution-worker` → sandbox container, with the
verdict appended to ClickHouse and the profile updated in Kvrocks.

## What is still server-authoritative (Phase 2)

The game ships a complete **local** economy, company layer, housing and social
graph so the loop is playable offline. Phase 2 moves the authoritative copies
to the services in `docs/design/05-system-architecture.md`:

- wallets, ledger, shop purchases, rent/upkeep and contract payouts (§5, §7)
- companies, hiring, escrow and reputation (§7)
- friends, teams, messages and presence (§4)
- housing layouts, trophies and visit permissions (§5)
- hidden-test content (already server-side in `execution-worker/`)

`game-server/` already implements the first slice of this — players, sessions,
the ledger, progression and mission grading (§1/§2/§5/§6) — against the data
tier. The game client still runs its local copies; wiring the shell to the API
is the next step, and each local path is already marked `[PHASE2]`. The C++
rules are written so the same functions can run on the server later: they take
plain state in and return plain results, with no rendering or I/O.

Equipment, housing benefits and company bonuses are **convenience only**. They
may change simulated build speed, mission slots, XP or comfort — never a test
verdict, an evidence score or an unlock condition.
