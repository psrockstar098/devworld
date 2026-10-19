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
│  Execution tiers:                                           │
│   • wasm    — in-browser: Python, JS, TS, SQL, Lua, Ruby,   │
│               PHP (lazy-loaded WASM runtimes)               │
│   • server  — sandbox backend (see below): C, C++, C#,      │
│               Java, Go, Rust, Zig, Kotlin, Swift, Dart,      │
│               Julia, Scala, Haskell                         │
│   • planned — R (runner code staged, unverified in CI env)  │
│   • highlight — editor-only (20 langs)                      │
└─────────────────────────────────────────────────────────────┘
                            │  (player-configured URL + API key)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  execution-worker/  (Bun + TypeScript, Docker)               │
│  POST /execute  {runtime, files[], stdin?, timeout?}         │
│  POST /assess   {runtime, files[], tests[] (hidden flag)}    │
│  GET  /runtimes (18-entry registry)                         │
│                                                             │
│  Per run: fresh container, --network none, --read-only,     │
│  --cap-drop ALL, no-new-privileges, pids/memory/cpu limits, │
│  wall-clock timeout, 256KB output cap, 5MB artifact cap.     │
│  API-key auth, per-key quotas (60/hr), concurrency cap (4). │
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

## Safe code execution

- Browser tier: WASM sandboxes (Pyodide, sql.js, Fengari, ruby.wasm,
  php-wasm) — no server, no cost, no escape to the player's machine
  beyond the page.
- Server tier: one-shot Docker containers per run (flags + limits above),
  API key + quotas + concurrency caps. Threat model lives in
  `execution-worker/docs/threat-model.md` — including what it does NOT
  cover (kernel escapes need gVisor/Firecracker; docker.sock = host root,
  so run the worker on a dedicated host).

## What Phase 1 does NOT include

Multiplayer, economy, companies, the 50-level ladder — those are the
north star. Phase 1 is: one town, one path, ten missions, real code.
The C++ game shell itself is still to come — the IDE is its terminal.
