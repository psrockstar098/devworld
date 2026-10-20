#!/usr/bin/env bash
# Bring up the DEVWORLD stores and the C++ platform server locally.
#   scripts/up.sh            # start stores, build if needed, start the server
#   scripts/up.sh --build    # force a rebuild first
#
# Uses podman when present, otherwise docker. Expects the containers created by
# infra/docker-compose.yml (names devworld-redis / devworld-kv / devworld-clickhouse).
set -euo pipefail
cd "$(dirname "$0")/.."

ROOT="$(cd .. && pwd)"
PORT="${PORT:-8091}"
LOG="${LOG:-/tmp/devworld-server.log}"
CH_URL="${CLICKHOUSE_URL:-http://127.0.0.1:8123}"
REDIS_PASSWORD="${REDIS_PASSWORD:-devworld_dev_pw}"
KV_PASSWORD="${KV_PASSWORD:-devworld_dev_pw}"

if command -v podman >/dev/null 2>&1; then
  CTR=podman
elif command -v docker >/dev/null 2>&1; then
  CTR=docker
else
  echo "need podman or docker" >&2
  exit 1
fi

echo "== stores ($CTR) =="
for name in devworld-redis devworld-kv devworld-clickhouse; do
  if "$CTR" inspect "$name" >/dev/null 2>&1; then
    "$CTR" start "$name" >/dev/null 2>&1 || true
    echo "  started $name"
  else
    echo "  missing $name — run: (cd infra && $CTR compose up -d)" >&2
    exit 1
  fi
done

echo "== waiting for stores =="
for i in $(seq 1 60); do
  ch=0; rd=0; kv=0
  curl -fsS --max-time 1 "$CH_URL/ping" >/dev/null 2>&1 && ch=1
  "$CTR" exec devworld-redis redis-cli -a "$REDIS_PASSWORD" --no-auth-warning ping 2>/dev/null | grep -q PONG && rd=1
  "$CTR" exec devworld-kv redis-cli -p 6666 -a "$KV_PASSWORD" --no-auth-warning ping 2>/dev/null | grep -q PONG && kv=1
  if [ "$ch" = 1 ] && [ "$rd" = 1 ] && [ "$kv" = 1 ]; then
    echo "  clickhouse, redis, kvrocks ready"
    break
  fi
  if [ "$i" = 60 ]; then
    echo "  stores did not become ready (ch=$ch redis=$rd kv=$kv)" >&2
    exit 1
  fi
  sleep 1
done

if [ "${1:-}" = "--build" ] || [ ! -x build/devworld-server ]; then
  echo "== build =="
  ./build.sh
fi

if curl -fsS --max-time 2 "http://127.0.0.1:${PORT}/health" >/dev/null 2>&1; then
  if [ "${1:-}" != "--build" ]; then
    echo "== server already listening on :${PORT} =="
    curl -sS "http://127.0.0.1:${PORT}/health"
    echo
    exit 0
  fi
  echo "== restarting server =="
  pkill -x devworld-server 2>/dev/null || true
  sleep 0.5
fi

echo "== server =="
CONTENT_DIR="${CONTENT_DIR:-$ROOT/game-shell/content}" \
  PORT="$PORT" nohup ./build/devworld-server >"$LOG" 2>&1 &
sleep 1
if ! curl -fsS --max-time 3 "http://127.0.0.1:${PORT}/health"; then
  echo
  echo "server did not answer; see $LOG" >&2
  tail -20 "$LOG" >&2
  exit 1
fi
echo
echo "log: $LOG"
