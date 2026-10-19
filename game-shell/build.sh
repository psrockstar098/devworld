#!/bin/bash
# Build the DEVWORLD game shell: C++17 -> WebAssembly via Emscripten.
# Usage: ./build.sh   (run after `source ~/emsdk/emsdk_env.sh`)
set -e
cd "$(dirname "$0")"

if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found. Run: source ~/emsdk/emsdk_env.sh"
  exit 1
fi

mkdir -p dist

echo "== compiling =="
em++ src/main.cpp src/game_init.cpp src/game_loop.cpp src/game_logic.cpp \
     src/world.cpp src/bridge.cpp \
  -std=c++17 -O2 \
  -s USE_SDL=2 -s USE_SDL_TTF=2 \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_main","_dw_ui_event","_dw_mission_result","_dw_set_keybind","_dw_touch","_dw_panel_json","_dw_ide_ready","_dw_resize","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString"]' \
  --preload-file content \
  --preload-file assets \
  -o dist/game.js

echo "== copying shell page =="
cp shell/index.html shell/shell.css shell/shell.js dist/

echo "== done =="
ls -la dist/game.js dist/game.wasm 2>/dev/null
echo "Serve: cd dist && python3 -m http.server 8000"
