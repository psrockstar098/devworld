# DEVWORLD game shell — build guide

The game shell is C++17 compiled to WebAssembly with Emscripten. It renders
the 2D world with SDL2 and drives DOM panels (HUD, dialogs, quest log, shop…)
through the `window.DW` bridge in `shell/shell.js`. The coding workspace is the
real VS Code IDE, hosted in an `<iframe>` — the shell bridges to it, never
reimplements it.

## Toolchain

- Emscripten SDK (`emsdk`), latest. Install once:
  ```
  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
  cd ~/emsdk
  ./emsdk install latest
  ./emsdk activate latest
  source ./emsdk_env.sh
  ```
- SDL2 and SDL_ttf come from Emscripten ports (`-s USE_SDL=2 -s USE_SDL_TTF=2`);
  they build automatically on first compile (needs network once, then cached
  in `~/emsdk/.cache`).
- Needs ~2 GB free RAM for the link step. If `emcc` dies with OOM, close
  other work first.

## Build

From `game-shell/`:

```
source ~/emsdk/emsdk_env.sh
./build.sh
```

`build.sh` compiles `src/*.cpp` to `dist/game.js` + `dist/game.wasm`, preloads
`content/` (missions, NPCs, maps — all data, no recompile to edit) and
`assets/DejaVuSans.ttf` into the virtual filesystem, and copies the shell page
(`shell/index.html`, `shell.css`, `shell.js`) into `dist/`.

To also bundle the IDE for the overlay (`dist/ide/`), copy a built
`vscode-ide/dist` tree:

```
cp -r ../vscode-ide/dist dist/ide
```

## Serve

The page needs HTTP (WASM + `file://` don't mix):

```
cd dist && python3 -m http.server 8000
```

then open http://localhost:8000/.

## Layout

- `src/` — C++17: `main.cpp` (entry + ccall exports), `game.h`,
  `game_init.cpp` (init/content/save), `game_loop.cpp` (update/render/input),
  `game_logic.cpp` (missions/XP/ledger/dialogue), `world.h/.cpp` (tilemaps),
  `bridge.h/.cpp` (EM_JS page bridge), `json.h` (content parser), `types.h`,
  `util.h`.
- `content/` — JSON data: `town.json`, `interiors.json`, `npcs.json`,
  `missions.json`, `shop.json`. Edit freely; no recompile needed (but the
  preload pack rebuilds on `./build.sh`).
- `shell/` — the page: `index.html`, `shell.js` (`window.DW`), `shell.css`.
- `dist/` — build output. Not committed (except for releases).

## Design notes (from the game design docs)

- **Persistence** (`devworld.save.v1` in localStorage): location, character,
  inventory, missions, ledger, achievements, reputation, flags. The ledger is
  double-entry; balances are derived. Phase 1 is local by design (single
  player); the architecture doc (§5) makes the server authoritative in Phase 2
  — code comments mark every spot that changes.
- **Mission loop** ("Proof of Skill"): accept at the board → starter code +
  tests are seeded into the IDE (`devworld.tests.v1` in the IDE frame's
  localStorage) → player codes in real VS Code → Submit runs the IDE's own
  test path (`GameIDE.runTests()`) → `{passed,total}` returns to C++ → XP,
  credits, quest log, unlocks. Hidden tests reveal only pass/fail.
- **Multiplayer**: region/instance fields exist in the data model, but Phase 1
  is single-player. Friends/company panels are honest "Phase 2" stubs.
