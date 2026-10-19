#!/usr/bin/env bash
# End-to-end round-trip check against all three stores.
# Verifies: ClickHouse insert+select, the ledger materialized view and its
# double-entry invariant, Redis set/get, and Kvrocks (RocksDB) set/get.
#
#   ./scripts/smoke.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -f .env ]; then set -a; . ./.env; set +a; fi
CH_USER="${CLICKHOUSE_USER:-devworld}"
CH_PW="${CLICKHOUSE_PASSWORD:-devworld_dev_pw}"
CH_DB="${CLICKHOUSE_DB:-devworld}"
REDIS_PW="${REDIS_PASSWORD:-devworld_dev_pw}"
KV_PW="${KV_PASSWORD:-devworld_dev_pw}"

run_id="$(date +%s)-$$"
fail=0
say() { printf '  %-11s %s\n' "$1" "$2"; }
ch() { docker exec devworld-clickhouse clickhouse-client --user "$CH_USER" --password "$CH_PW" "$@"; }

echo "DEVWORLD store smoke test (run ${run_id})"

# --- ClickHouse: event round trip -------------------------------------------
ch --query "INSERT INTO ${CH_DB}.events (player_id, event, props) VALUES ('smoke-${run_id}', 'smoke', '{\"run\":\"${run_id}\"}')"
n="$(ch --query "SELECT count() FROM ${CH_DB}.events WHERE player_id='smoke-${run_id}'")"
if [ "$n" = "1" ]; then say clickhouse "OK  (events insert+select)"; else say clickhouse "FAIL count=$n"; fail=1; fi

# --- ClickHouse: ledger is double-entry and the balance MV is correct --------
ch --multiquery --query "
  INSERT INTO ${CH_DB}.ledger_entries (ref, player_id, account, amount) VALUES
    ('smoke-${run_id}', 'smoke-${run_id}', 'wallet', 100),
    ('smoke-${run_id}', 'smoke-${run_id}', 'escrow', -100);"
net="$(ch --query "SELECT sum(amount) FROM ${CH_DB}.ledger_entries WHERE ref='smoke-${run_id}'")"
wallet="$(ch --query "SELECT sum(balance) FROM ${CH_DB}.mv_player_balance WHERE player_id='smoke-${run_id}' AND account='wallet'")"
if [ "$net" = "0" ] && [ "$wallet" = "100" ]; then
  say clickhouse "OK  (ledger balanced; wallet MV=$wallet)"
else
  say clickhouse "FAIL ledger net=$net wallet_mv=$wallet"; fail=1
fi

# --- Redis: cache round trip -------------------------------------------------
docker exec devworld-redis redis-cli -a "$REDIS_PW" --no-auth-warning SET "smoke:${run_id}" ok >/dev/null
rv="$(docker exec devworld-redis redis-cli -a "$REDIS_PW" --no-auth-warning --raw GET "smoke:${run_id}")"
docker exec devworld-redis redis-cli -a "$REDIS_PW" --no-auth-warning DEL "smoke:${run_id}" >/dev/null
if [ "$rv" = "ok" ]; then say redis "OK  (set+get)"; else say redis "FAIL got=$rv"; fail=1; fi

# --- Kvrocks (RocksDB): durable KV round trip --------------------------------
# The Kvrocks image has no redis-cli, so borrow the Redis image on the same net.
kv() { docker run --rm --network devworld redis:7-alpine redis-cli -h kv -p 6666 -a "$KV_PW" --no-auth-warning "$@"; }
kv SET "smoke:${run_id}" rocks >/dev/null
kvval="$(kv --raw GET "smoke:${run_id}")"
kv DEL "smoke:${run_id}" >/dev/null
if [ "$kvval" = "rocks" ]; then say kvrocks "OK  (rocksdb set+get)"; else say kvrocks "FAIL got=$kvval"; fail=1; fi

echo
if [ "$fail" -eq 0 ]; then echo "smoke test PASSED"; else echo "smoke test FAILED"; exit 1; fi
