# DEVWORLD data infrastructure

Three stores, each with one job. Everything here is local-development default;
override the credentials in a `.env` (see `.env.example`) before anything real.

| Service | Image | Role | Port(s) |
|---|---|---|---|
| **clickhouse** | `clickhouse/clickhouse-server:25.8` | **Main database.** Columnar, append-only: events, telemetry, the double-entry ledger, XP, mission attempts, assessments, moderation. History, not current state. | 8123 (HTTP) · 9000 (native) |
| **kv** (Kvrocks) | `apache/kvrocks:latest` | **RocksDB store.** Durable current-state key-value: players, sessions, inventory, submissions, job/company records. RocksDB engine behind a Redis-compatible protocol. | 6666 |
| **redis** | `redis:7-alpine` | **Cache / realtime.** Sessions and hot reads with TTL, rate-limit counters, pub/sub for presence and chat. Disposable. | 6379 |

Why this split: ClickHouse is excellent at "what happened" (wide scans, aggregates,
retention) and poor at hot single-key updates; RocksDB is the opposite. Redis is
fast and volatile, so it holds only things we can recompute or re-fetch.

## Run it

```bash
cd infra
./scripts/up.sh                 # compose up + wait for healthy + smoke test
```

Or step by step:

```bash
docker compose up -d
./scripts/healthcheck.sh        # waits until all three report healthy
./scripts/smoke.sh              # round-trips each store, checks ledger invariant
```

Stop / reset:

```bash
docker compose down             # keep data volumes
docker compose down -v          # wipe data (re-runs the ClickHouse init scripts)
```

The embedded-RocksDB proof (the actual `librocksdb` C++ library, not Kvrocks):

```bash
./scripts/rocksdb-embedded-test.sh
```

## ClickHouse schema

Initialised once on an empty data volume from `clickhouse/init/`:

- `events` — generic event stream (`player_id`, `event`, `service`, `props` JSON).
- `ledger_entries` — double-entry ledger; **the rows of one `ref` sum to zero**
  and balances are derived.
- `xp_events` — append-only XP; level is derived from the total.
- `mission_attempts` — one row per grading attempt (`public_passed`, `hidden_passed`).
- `assessment_events` — raw execution-worker `/assess` events.
- `moderation_signals` — the worker's abuse feed.
- Materialized views: `mv_player_balance` (balance per player **and account**,
  so a wallet credit and its `system` counter-entry do not cancel out),
  `mv_player_xp` (lifetime XP), `mv_mission_pass` (attempts/passes per mission).

Because init scripts only run on a fresh volume, schema changes during
development are applied by editing the SQL and running `docker compose down -v`
(or by adding a numbered migration and rebuilding the volume).

## Ports and access

All ports are published on `127.0.0.1` only. Default dev credentials are
`devworld` / `devworld_dev_pw`; they are footguns, not secrets. Compose also
creates a bridge network named `devworld` so application services
(`game-server/`, `execution-worker/`) can reach the stores by hostname:
`clickhouse`, `kv`, `redis`.
