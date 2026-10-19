#!/bin/bash
# DEVWORLD - Ubuntu setup script.
# One command to go from a fresh Ubuntu install to building everything:
#   game shell (C++ -> WASM), the VS Code IDE, and the execution worker.
#
# Usage:  ./setup-ubuntu.sh
# Run from the repo root (the folder containing this script).
# Some steps need sudo - the script will ask when it needs it.
set -e

echo "=== DEVWORLD Ubuntu setup ==="
echo ""

# --- 0. sanity: Ubuntu + repo root -------------------------------------------
if [ ! -f /etc/os-release ] || ! grep -qi ubuntu /etc/os-release; then
  echo "This script is for Ubuntu. Aborting."
  exit 1
fi
if [ ! -d game-shell ] || [ ! -d vscode-ide ]; then
  echo "Run this from the devworld repo root (where game-shell/ and vscode-ide/ live)."
  exit 1
fi

# --- 1. system packages -------------------------------------------------------
echo "--- [1/6] system packages ---"
sudo apt update
sudo apt install -y curl git python3 build-essential cmake pkg-config \
  ca-certificates gnupg

# --- 2. Node.js 22 LTS (for the vscode-ide build) -----------------------------
echo "--- [2/6] Node.js ---"
if command -v node >/dev/null 2>&1 && node -v | grep -qE "v(2[2-9]|[3-9][0-9])"; then
  echo "node $(node -v) already installed, skipping."
else
  curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
  sudo apt install -y nodejs
fi
node -v && npm -v

# --- 3. Bun (for the execution worker) ----------------------------------------
echo "--- [3/6] Bun ---"
if ! command -v bun >/dev/null 2>&1; then
  BUN_URL="https://bun."sh/install
  curl -fsSL "$BUN_URL" | bash
fi
export PATH="$HOME/.bun/bin:$PATH"
bun -v

# --- 4. Emscripten SDK (for the C++ game shell) -------------------------------
echo "--- [4/6] Emscripten SDK (~1GB download, takes a few minutes) ---"
if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
  echo "emsdk already present at ~/emsdk, skipping install."
else
  git clone https://github.com/emscripten-core/emsdk.git "$HOME/emsdk"
  ( cd "$HOME/emsdk" && ./emsdk install latest && ./emsdk activate latest )
fi
# shellcheck disable=SC1091
source "$HOME/emsdk/emsdk_env.sh" >/dev/null
emcc -v

# --- 5. Docker (for the execution worker sandbox) -----------------------------
echo "--- [5/6] Docker ---"
if command -v docker >/dev/null 2>&1; then
  echo "docker already installed, skipping."
else
  sudo apt install -y docker.io
  sudo systemctl enable --now docker 2>/dev/null || sudo service docker start 2>/dev/null || true
  sudo usermod -aG docker "$USER" || true
  echo "NOTE: log out and back in for docker group membership to apply."
fi
docker -v || true

# --- 6. build everything ------------------------------------------------------
echo "--- [6/6] building the project ---"

echo ">> vscode-ide (real VS Code workbench)"
( cd vscode-ide && npm install && npm run build )
echo "vscode-ide built -> vscode-ide/dist/"

echo ">> game shell (C++ -> WebAssembly)"
( cd game-shell && ./build.sh )
echo "game shell built -> game-shell/dist/"

echo ">> game shell tests (native + content)"
( cd game-shell && ./tests/run_tests.sh )

echo ""
echo "=== DONE ==="
echo "Serve locally:"
echo "  IDE:        cd game-ide  && python3 -m http.server 8000   (deployed copy)"
echo "              (or rebuild:  cd vscode-ide && npm run build, then copy dist/* to game-ide/)"
echo "  Game shell: cd game-shell/dist && python3 -m http.server 8001"
echo ""
echo "Data tier (ClickHouse + RocksDB/Kvrocks + Redis, needs Docker running):"
echo "  cd infra && cp .env.example .env && ./scripts/up.sh"
echo "  (optional) ./scripts/rocksdb-embedded-test.sh   # prove real librocksdb works"
echo ""
echo "Game server (needs the data tier up):"
echo "  cd game-server && bun install && cp .env.example .env && bun server.ts   # :8091"
echo ""
echo "Execution worker (needs Docker running):"
echo "  cd execution-worker && bun install && bun run pull-runtimes && docker compose up"
echo "  (set KV_URL + CLICKHOUSE_URL to persist submissions and grading events)"
