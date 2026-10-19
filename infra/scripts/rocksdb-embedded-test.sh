#!/usr/bin/env bash
# Build and run the embedded-RocksDB proof (real librocksdb, C++).
# The program asserts its invariants and exits non-zero on failure; the
# Dockerfile also runs it at build time, so a broken build == a failed test.
#
#   ./scripts/rocksdb-embedded-test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

docker compose --profile tools build rocksdb-embedded
out="$(docker compose --profile tools run --rm --no-deps rocksdb-embedded)"
echo "$out"
if echo "$out" | grep -q '"ok":true'; then
  echo "embedded RocksDB: OK"
else
  echo "embedded RocksDB: FAILED" >&2
  exit 1
fi
