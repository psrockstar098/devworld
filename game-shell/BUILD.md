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
  `game_init.cpp` (init/content/save), `game_loop.cpp` (update/render/input,
  minimap), `game_logic.cpp` (missions/XP/ledger/dialogue/travel/identity/
  open-world panels), `world.h/.cpp` (tilemaps, cities, rendering),
  `world_model.h/.cpp` (pure city graph + travel rules), `progression.h/.cpp`
  (pure XP/level/gating rules), `equipment.h/.cpp` (pure slots + convenience
  bonuses), `profile.h/.cpp` (pure profile summary + notice log),
  `settings.h/.cpp` (pure accessibility prefs), `regions.h/.cpp` (pure regional
  economies), `dungeons.h/.cpp` (pure discovery/gating/rewards),
  `housing.h/.cpp` (pure home slots + derived benefits), `social.h/.cpp` (pure
  contacts, deterministic replies, teams), `company.h/.cpp` (pure company rules),
  `portfolio.h/.cpp` (derived portfolio + evidence score),
  `ambience.h/.cpp` + `audio.h/.cpp` (procedural day/night environmental audio),
  `bridge.h/.cpp` (EM_JS page bridge), `json.h` (content parser), `types.h`,
  `util.h`.
- `content/` — JSON data: `town.json`, `cities.json` (Startup City, Systems
  District, Innovation Capital), `world.json` (city graph + bus fares + map
  ownership + regional economies), `interiors.json`, `npcs.json`,
  `missions.json`, `shop.json`, `dungeons.json`, `housing.json`,
  `contacts.json`. Edit freely; no recompile needed (but the preload pack
  rebuilds on `./build.sh`).
- `shell/` — the page: `index.html`, `shell.js` (`window.DW`), `shell.css`.
- `tests/` — native + WASM suites and mission reference solutions; see
  `tests/README.md`.
- `dist/` — build output. Not committed (except for releases).

## Tests

```
./tests/run_tests.sh        # native C++ + content + mission solutions
./tests/run_wasm_tests.sh   # the same C++ suite as WebAssembly under node
```

Both run in CI (`.github/workflows/game-shell-tests.yml`).

## Design notes (from the game design docs)

- **Persistence** (`devworld.save.v1` in localStorage): location, character,
  inventory, missions, ledger, achievements, reputation, flags, home layout,
  displayed trophies, messages, unread counts, team and company. The ledger is
  double-entry; balances are derived. Phase 1 is local by design (single
  player); the architecture doc (§5) makes the server authoritative in Phase 2
  — code comments mark every spot that changes.
- **Mission loop** ("Proof of Skill"): accept at the board → starter code +
  tests are seeded into the IDE (`devworld.tests.v1` in the IDE frame's
  localStorage) → player codes in real VS Code → Submit runs the IDE's own
  test path (`GameIDE.runTests()`) → `{passed,total}` returns to C++ → XP,
  credits, quest log, unlocks. Hidden tests reveal only pass/fail.
- **World & travel**: `world.json` is the city graph (cities, bus links, fares,
  which maps belong to which city). Bus stops open the fast travel panel;
  a route unlocks on level + a proven mission (`world_model.h`). Interiors are
  solo instances; exteriors are shared regions (`regionId` / `instanceType` on
  every map, saved with the player). Minimap + M-map are navigation aids;
  travel itself only happens at a bus stop.
- **Ambience**: wind, warm pad, crickets and room hum are synthesized in the
  audio callback (`ambience.h` is the testable mix table). Day/night and the
  city you are in change the mix; interiors swap wind for hum.
- **Identity & interaction**: character creation (name, display name, skin,
  hair, shirt, pants) is saved with the profile. The profile panel derives
  everything from state (`profile.h`), including reputation tier. Interactables
  include NPCs, computers, doors, boards, banks, buses, mail and inspectable
  objects with lore.
- **Equipment is convenience only** (`equipment.h`): slots are `computer`,
  `tool`, `decor`; effects add mission slots, bonus mission XP, simulated build
  speed or comfort. The content test pins the allowed effect keys, so nothing
  that could change a test verdict can ship.
- **Quest tracking & notifications**: one tracked mission drives the HUD
  objective (`objectiveFor` in `progression.h`); every player message is kept in
  a capped notification log (`profile.h`).
- **Accessibility**: UI scale, text size, reduced motion, color-blind-safe
  palette, high contrast and remappable controls. Prefs are clamped in
  `settings.h` (unit-tested) and applied both to the C++ renderer and the DOM.
- **Open world (2.3)**: `regions.h` holds living cost, market bias, job focus
  and community per city; shop prices and bus fares read it. `dungeons.h`
  defines a hatch on a city exterior, a discovery flag, a level gate, a mission
  and a rare reward. Rumours in NPC dialogue set the flag; unrevealed hatches
  are invisible to interaction and the minimap. `housing.h` owns typed slots
  (workstation/decor/trophy) and derives benefits; `social.h` owns contacts,
  deterministic keyword replies, unread counts and 3-person teams;
  `company.h` owns founding, rank and a convenience payout preview;
  `portfolio.h` derives the project list and evidence score from mission state.
- **Multiplayer**: region/instance fields exist in the data model and the social
  and company layers are shaped for it, but Phase 1 is single-player. Contacts
  are written characters with deterministic replies, and every server-authority
  path is marked `[PHASE2]`.
