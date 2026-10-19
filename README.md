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
  native-only, no browser bundle), SQL (sql.js 1.15.0), Lua (Fengari, Lua 5.3)
- Language picker, run button, output console, per-language hello-world samples
- `window.GameIDE` API so the C++ game shell can drive it later:
  `setLanguage()`, `getCode()`, `setCode()`, `runCode()`, `onRun()`

Open `game-ide/index.html` in a browser — no build step, no server.

### Execution tiers

| Tier | Meaning | Languages |
|------|---------|-----------|
| `wasm` | Runs in the browser today | Python, JavaScript, TypeScript, SQL, Lua |
| `planned` | WASM runner exists, wiring later | Ruby, PHP, R |
| `server` | Needs a sandboxed backend later | C, C++, C#, Java, Go, Rust, Zig, Kotlin, Swift, Dart, Julia, Scala, Haskell, … |
| `highlight` | Editor support only for now | Assembly, Bash, Scratch, Q#, LabVIEW, … |

See `game-ide/languages.json` for the full registry.

## Roadmap

1. ✅ Game IDE (this step)
2. C++ game shell (Emscripten + WebGL): starter town, player, mission board
3. IDE ↔ game bridge: missions defined in C++, code checked by runners
4. First 10 missions, one language path
5. Server sandbox for compiled languages
6. Multiplayer / economy (later)

## Docs

- `docs/architecture.md` — how the pieces fit together
