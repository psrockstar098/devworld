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
  `dungeons.*`, `housing.*`, `social.*`, `company.*`, `portfolio.*`,
  `curriculum.*`, `mastery.*`, `capability.*`, `unlocks.*`, `achievements.*`,
  `rewards.*` and `careers.*` have no
  SDL, Emscripten or I/O, so the same code runs in the game and in the native +
  WASM test suites (`game-shell/tests/`).
- **Mastery is derived, never stored.** The game records an append-only
  evidence log; the education model (`mastery.*`, design 08) computes skill
  scores, independence and spaced review from it. XP and equipment never enter
  the calculation.
- **Progression is derived from capability.** The progression engine
  (`capability.*`, `unlocks.*`, design 09) snapshots what the player can
  actually do — level, mastery, language proficiency, career, reliability,
  achievements, company leadership kept as separate records — and evaluates
  every open-world opportunity against that snapshot, never against the level.
  Each unlock returns the reason it was earned or missing; XP rewards
  (`rewards.*`) are paid only for meaningful work.
- **IDE stays web-native (Monaco).** Rebuilding VS Code's editor in C++
  would be madness — Monaco *is* VS Code's editor. The game shell drives
  it through the `window.GameIDE` JS bridge (Emscripten `EM_JS` /
  `emscripten::val`).
- **Execution in tiers.** Interpreted languages run free in the browser
  via WASM runtimes (lazy-loaded so first paint stays fast). Compiled
  languages go to the sandbox worker: isolated containers, resource
  limits, no network — player code never runs on the game server itself.

## Mission flow (the core loop)

1. Player walks to the mission board in the starter town, or opens the
   **Code Workspace** on the in-game computer (C++ side).
2. Game calls `GameIDE.setLanguage('python')` + `setCode(starter)`.
3. Player writes code, hits Run (or the game triggers `runCode()`).
4. `onRun` callback reports pass/fail back to the C++ shell.
5. `GameIDE.runTests()` grades against public + hidden tests —
   hidden results reveal only pass/fail ("Proof of Skill").
6. With the platform session live, **Verify on Server** sends the whole
   workspace to `/missions/:id/submit`; the server puts the mission's own file
   first regardless of workspace order and returns the authoritative verdict.
7. Rewards: XP, in-game money, unlocks — paid by the server ledger and
   reflected in the local shell.

With the backend running (`infra/` + `server/` + `execution-worker/`), the
same submission is graded authoritatively: the client posts to `server`,
which forwards to the worker's `/assess` (hidden tests never leave the server),
then writes the attempt, XP and a balanced ledger transaction to ClickHouse and
the updated profile to Kvrocks. The local shell grading is the offline preview;
the worker is the source of truth.

## Shared world, presence and social layer

There is one Starter Town, not one town per account. Exteriors are shared maps:
every signed-in player in a town or city is published into the same presence map
and rendered in the C++ engine as a `RemotePlayer` (body, hair, clothes, facing
and walk state). Interiors — an apartment, a lab, a dungeon — remain private
instances, so a player can step off the street without being followed.

- **Presence is ephemeral.** The server keeps an in-process registry of who is
  where, mirrored into Redis with a TTL. The client publishes a position
  heartbeat from the game loop (not from a menu callback), so opening the
  phone, IDE or mission board does not look like logging off. `/world/players`
  exposes the same list for non-WebSocket clients.
- **Accounts are real.** Registration and login are username/password
  (PBKDF2-HMAC-SHA256) with opaque session tokens. A player record in Kvrocks
  holds the durable identity, level inputs, appearance and `homeUnit`; no
  seeded or demo humans appear in the town. NPC contacts stay explicitly
  labelled as NPCs.
- **Every account has an address.** The first login assigns a Home Row unit
  (`INCR world:home:cursor` in Redis); older accounts are migrated lazily the
  next time they sign in. The unit number is public on the profile and is
  shown on the phone and the apartment computer.
- **Appearance is durable.** Skin, shirt, pants and hair live on the player
  record, are returned by `/me` and public profiles, and are changed through
  `PUT /me/appearance`. The server validates the four colour triples, echoes
  the new `player` object, and broadcasts `player.updated` so everyone already
  in town sees the change on the remote sprite.
- **Friendships are durable.** `social:<playerId>` in Kvrocks stores
  `friends`, `incoming` and `outgoing`. Request / accept / decline / remove are
  HTTP endpoints; the receiving tab gets `friend.request` / `friend.accepted`
  over the socket, and the friends lists repaint without a page reload.
- **Chat is live and bounded.** `chat.send` on `/ws` is rate-limited, assigned
  a server timestamp and author, appended to a short recent-history ring, and
  broadcast to every connected account. `world.snapshot` gives a newcomer the
  current players and recent chat immediately on arrival.
- **The socket carries the world.** `world.snapshot`, `world.move`,
  `chat.message`, `player.updated` and the friend events use the existing hub;
  there is no second realtime transport.

The page is a device layer over that world, not a substitute for it:

- The **in-game computer** (`NOVA-80`) is a physical object in the apartment
  and the lab. Interacting with it opens a desktop with **Code Workspace**
  (the real Monaco IDE and mission submit bar), **Mission Board**, **Town
  Network**, **Portfolio** and **Mail**. The desktop pulls its operator line
  from `profileJson()` and its live town state from `DW.cloud`.
- The **phone** (`F`, or the HUD **Phone** button) is the character's social
  device: home screen with Home Row and the last town message, plus **Chat**
  (town messages), **People** (online players and friend requests), **Me**
  (handle, unit, level, XP, credits), **Look** (appearance editor), **Map** and
  **Alerts**. Its live sections are painted incrementally, so an incoming
  message never overwrites what the player is typing.

Both devices mark the C++ world as owning input while open (Esc closes them),
and both use the same platform session and WebSocket as the rest of the shell.

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

Two services back the game, plus the stores they share (all under `infra/`):

- **`execution-worker/` (§3)** — runs untrusted code in one-shot containers. The
  only service that executes player code. Grading outcomes and moderation
  signals go to ClickHouse (best-effort, never blocking a grade); submission
  documents persist to RocksDB (Kvrocks) behind the same `SubmissionStore`
  interface.
- **`server/` (§1/§2/§5/§6)** — the platform API, native C++17 with no
  third-party libraries: players, username/password sessions, the ledger,
  derived progression and mission grading over HTTP + WebSocket. It forwards
  submissions to the worker's `/assess` and never executes code itself.
  The earlier Bun implementation (`game-server/`) is retained as a behavior
  reference, not as the production path.

Stores (see `infra/README.md`):

| Store | Role |
|---|---|
| **ClickHouse** | History: events, the double-entry ledger, XP, attempts, assessments, moderation. Append-only. |
| **RocksDB (Kvrocks)** | Durable current state: player records, submission/revision documents. |
| **Redis** | Disposable: session tokens, read caches, rate limits, pub/sub. |

`cd infra && ./scripts/up.sh` starts and proves the stores. The request path is
client → `server/` → `execution-worker/` → sandbox container, with the
verdict appended to ClickHouse and the profile updated in Kvrocks.

The game shell is wired to `server/` for identity, the shared world and live
profile data: the ☁ chip / **Cloud account** panel registers and signs in with
username/password, reads `/me`, `/missions` and the derived balance, and keeps a
WebSocket open for `mission.completed`, world/social events and chat. The token
is opaque and stored in `localStorage`; the server owns the password hash, XP
and ledger. **Verify on Server** in the IDE is the authoritative grading path
whenever `WORKER_URL` is configured; the local test runner remains the offline
preview.

## What is still server-authoritative (Phase 2)

The game ships a complete **local** economy, company layer, housing and social
graph so the loop is playable offline. Phase 2 moves the authoritative copies
to the services in `docs/design/05-system-architecture.md`:

- wallets, ledger, shop purchases, rent/upkeep and contract payouts (§5, §7)
- companies, hiring, escrow and reputation (§7)
- teams and direct messages (§4) — public presence, friendships and town chat
  are already live on `server/`
- housing layouts, trophies and visit permissions (§5) — Home Row unit
  assignment is already server-side
- hidden-test content (already server-side in `execution-worker/`)

`game-server/` already implements the first slice of this — players, sessions,
the ledger, progression and mission grading (§1/§2/§5/§6) — against the data
tier. Its C++ replacement in `server/` is now the live API, and the shell's
Cloud account panel uses it for auth, profile, progression and balance. The
rest of the game client still runs its local copies; each local path is already
marked `[PHASE2]`. The C++ rules are written so the same functions can run on
the server later: they take plain state in and return plain results, with no
rendering or I/O.

Equipment, housing benefits and company bonuses are **convenience only**. They
may change simulated build speed, mission slots, XP or comfort — never a test
verdict, an evidence score or an unlock condition.
