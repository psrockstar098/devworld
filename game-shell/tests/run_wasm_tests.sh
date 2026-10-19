#!/bin/bash
# Compile and run the same C++ test suite as WebAssembly via Emscripten.
# Usage: ./tests/run_wasm_tests.sh [doctest args...]
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v em++ >/dev/null 2>&1; then
  if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "$HOME/emsdk/emsdk_env.sh" >/dev/null
  fi
fi
if ! command -v em++ >/dev/null 2>&1; then
  echo "em++ not found. Install the Emscripten SDK (see setup-ubuntu.sh) or"
  echo "source ~/emsdk/emsdk_env.sh first."
  exit 1
fi

OUT="tests/build/wasm"
mkdir -p "$OUT"

echo "== building WASM tests =="
# -fexceptions: json.h throws and the game catches (corrupt-save recovery);
# without it Emscripten turns throw into abort().
em++ -std=c++17 -O1 -fexceptions \
  -I src -I tests -I tests/third_party \
  tests/test_main.cpp tests/test_json.cpp tests/test_progression.cpp \
  tests/test_world_model.cpp tests/test_ambience.cpp \
  tests/test_equipment.cpp tests/test_profile.cpp tests/test_settings.cpp \
  tests/test_regions.cpp tests/test_dungeons.cpp tests/test_housing.cpp \
  tests/test_social.cpp tests/test_company.cpp tests/test_portfolio.cpp \
  tests/test_content.cpp \
  src/progression.cpp src/world_model.cpp src/ambience.cpp \
  src/equipment.cpp src/profile.cpp src/settings.cpp \
  src/regions.cpp src/dungeons.cpp src/housing.cpp \
  src/social.cpp src/company.cpp src/portfolio.cpp \
  --preload-file content \
  -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 \
  -o "$OUT/tests.js"

echo "== running WASM tests under node =="
# Run from the output dir so the preloaded tests.data is found next to the js.
( cd "$OUT" && node tests.js "$@" )
