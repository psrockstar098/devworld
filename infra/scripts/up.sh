#!/usr/bin/env bash
# Bring the whole data tier up and prove it works.
#
#   ./scripts/up.sh          # start + healthcheck + smoke
#   ./scripts/up.sh --no-smoke
set -euo pipefail
cd "$(dirname "$0")/.."

docker compose up -d
./scripts/healthcheck.sh

if [ "${1:-}" != "--no-smoke" ]; then
  ./scripts/smoke.sh
fi
