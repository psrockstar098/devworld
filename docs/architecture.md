# Architecture — DEVWORLD

## The shape

```
┌─────────────────────────────────────────────────────┐
│  Browser                                            │
│                                                     │
│  ┌──────────────┐      ┌────────────────────────┐   │
│  │ C++ game     │◄────►│ Game IDE (Monaco)      │   │
│  │ shell (WASM) │ API  │ window.GameIDE         │   │
│  │ via          │      │                        │   │
│  │ Emscripten   │      │ runners: Pyodide,      │   │
│  │ + WebGL      │      │ sql.js, Fengari,       │   │
│  │              │      │ native JS/TS           │   │
│  └──────────────┘      └────────────────────────┘   │
│         │                                           │
│         │ missions, progress, economy               │
│         ▼                                           │
│  ┌──────────────┐      ┌────────────────────────┐   │
│  │ Yatta-style  │      │ Sandbox API (later)    │   │
│  │ backend      │─────►│ compiled languages:    │   │
│  │ (optional)   │      │ C/C++/Rust/Go/Java…    │   │
│  └──────────────┘      └────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

## Why this split

- **Game shell in C++ → WASM.** Near-native speed, one codebase, runs
  anywhere. Emscripten gives us WebGL rendering and SDL-style input.
- **IDE stays web-native (Monaco).** Rebuilding VS Code's editor in C++
  would be madness — Monaco *is* VS Code's editor. The game shell drives
  it through the `window.GameIDE` JS bridge (Emscripten `EM_JS` /
  `emscripten::val`).
- **Execution in tiers.** Interpreted languages run free in the browser
  via WASM runtimes. Compiled languages need a server sandbox (isolated
  containers, resource limits) — that's Phase 2 infrastructure, not
  Phase 1.

## Mission flow (the core loop)

1. Player walks to the mission board in the starter town (C++ side).
2. Game calls `GameIDE.setLanguage('python')` + `setCode(starter)`.
3. Player writes code, hits Run (or the game triggers `runCode()`).
4. `onRun` callback reports pass/fail back to the C++ shell.
5. Mission checks run against hidden tests (runner-side assertions).
6. Rewards: XP, in-game money, unlocks — handled by the game shell.

## Safe code execution

- Browser tier: WASM sandboxes (Pyodide, sql.js, Fengari) — no server,
  no cost, no escape to the player's machine beyond the page.
- Server tier (later): one-shot containers, CPU/memory/time limits, no
  network. Never run player code on the game server itself.

## What Phase 1 does NOT include

Multiplayer, economy, companies, the 50-level ladder — those are the
north star. Phase 1 is: one town, one path, ten missions, real code.
