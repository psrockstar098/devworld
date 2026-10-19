#!/usr/bin/env bash
# Wait until ClickHouse, Kvrocks (RocksDB) and Redis all report healthy.
#
#   ./scripts/healthcheck.sh [timeout_seconds]
set -euo pipefail
cd "$(dirname "$0")/.."

timeout="${1:-180}"
declare -A containers=(
  [clickhouse]=devworld-clickhouse
  [kv]=devworld-kv
  [redis]=devworld-redis
)

deadline=$((SECONDS + timeout))
printf 'waiting up to %ss for: %s\n' "$timeout" "${!containers[*]}"

while :; do
  all=1
  for name in "${!containers[@]}"; do
    cid="${containers[$name]}"
    state="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$cid" 2>/dev/null || echo missing)"
    [ "$state" = "healthy" ] || all=0
  done

  if [ "$all" -eq 1 ]; then
    echo "all services healthy:"
    docker compose ps
    exit 0
  fi

  if [ "$SECONDS" -ge "$deadline" ]; then
    echo "timed out waiting for healthy services; current state:" >&2
    docker compose ps >&2 || true
    echo "--- last logs ---" >&2
    docker compose logs --tail=40 >&2 || true
    exit 1
  fi
  sleep 2
done
