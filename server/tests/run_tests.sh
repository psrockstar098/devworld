#!/bin/bash
# Unit + integration tests for the C++ platform server.
# Native g++, no third-party libraries. Real-store tests self-skip when the
# ClickHouse / Redis / Kvrocks services are not running.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="tests/build"
mkdir -p "$OUT"

SRC=()
for f in src/*.cpp; do
  [ "$(basename "$f")" = "main.cpp" ] && continue
  SRC+=("$f")
done

TEST=(tests/test_main.cpp)
for f in tests/test_*.cpp; do
  [ "$(basename "$f")" = "test_main.cpp" ] && continue
  TEST+=("$f")
done

echo "== building server tests =="
"$CXX" -std=c++17 -O1 -pthread -I src -I tests -I tests/third_party \
  "${TEST[@]}" "${SRC[@]}" -o "$OUT/tests"

echo "== running server tests =="
"$OUT/tests" "$@"
