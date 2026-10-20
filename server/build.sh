#!/bin/bash
# Build the DEVWORLD C++ platform server. No third-party libraries.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
mkdir -p build

echo "== building devworld-server =="
"$CXX" -std=c++17 -O2 -pthread -Wall -Wextra -I src \
  src/*.cpp -o build/devworld-server

echo "built: $(pwd)/build/devworld-server"
