# DEVWORLD game server

The game API: players, sessions, the **double-entry ledger**, progression, and
mission grading. It owns the hidden tests and the authoritative economics, and it
**never executes player code** — submissions are forwarded to the sandbox worker
(`execution-worker/`) and the verdict comes back.

## Stores

| Store | Client | Used for |
|---|---|---|
| **ClickHouse** (main) | `src/clickhouse.ts` — HTTP, no driver | Append-only history: events, `ledger_entries`, `xp_events`, `mission_attempts`, `assessment_events`, plus the `mv_*` views. |
| **RocksDB** (Kvrocks) | `src/kv.ts` (`RedisKv`, ioredis) | Durable current state: `player:<id>`, `handle:<handle>`. |
| **Redis** | `src/kv.ts` (`RedisKv`) | Disposable: `session:<token>` (TTL) and the `balance:<id>:<currency>` read cache. |

Balances and levels are **derived**, never stored twice: a wallet balance is
`SUM(amount) WHERE account='wallet'` over the ledger view, and a level is a fold
over total XP (`src/progression.ts`, which mirrors `game-shell/src/progression.cpp`
exactly — the tests pin the equivalence).

## Run it

Against the local data tier (`cd infra && ./scripts/up.sh` first):

```bash
bun install
cp .env.example .env          # defaults point at 127.0.0.1
bun server.ts                 # :8091
```

Or in Docker, on the shared `devworld` network:

```bash
cd infra && ./scripts/up.sh
cd game-server && docker compose up -d --build
```

## API

Auth is `Authorization: Bearer <token>` from `POST /auth/login`.

| Method | Path | Notes |
|---|---|---|
| GET | `/health` | Checks ClickHouse, Kvrocks and Redis; `503` if any is down. |
| POST | `/players` | `{ id, handle, displayName? }` — creates the record + handle reservation. |
| GET | `/players/:id` | Public subset (no flags/PII). |
| POST | `/auth/login` | `{ playerId }` → `{ token, expiresInSec }` (Redis TTL). |
| POST | `/auth/logout` | Invalidates the token. |
| GET | `/me` · `/progression/me` | Profile + progression + balance. |
| GET | `/missions` | Catalogue; **hidden tests carry no data**. |
| POST | `/missions/:id/submit` | `{ files: [{ path, content }] }` → grade via the worker, then reward. |
| GET | `/ledger/balance` | Wallet balance (cached). |
| POST | `/admin/ledger` | `x-admin-key`; posts a balanced transaction. |
| GET | `/stats/missions` | Attempts/passes per mission from `mv_mission_pass`. |
| POST | `/presence/heartbeat` | `{ city, map? }` — reports the caller's location; entries expire after 120 s without a heartbeat. |
| GET | `/presence/online` | Players online now, with handle, city, map and `lastSeenMs`. |
| POST | `/events` | `x-admin-key`; records a world event `{ type, title, detail?, actorId?, city? }`. |
| GET | `/events` | World event log, newest-first; `?type=` filter, `?limit=` (max 100). |

### Submission flow

1. Authenticate; check the mission exists and is not already completed.
2. Enforce `requires` (prerequisites) and the `level` gate.
3. Validate files (≤20 files, ≤200 KB each, ≤1 MB total, safe relative paths).
4. `POST /assess` to the worker with public **and** hidden tests.
5. Summarise: a full pass grants XP (`xp_events`) and credits (a balanced
   `system`→`wallet` ledger transaction), and marks the mission complete in
   Kvrocks. The response only ever contains public test detail; hidden tests are
   pass/fail only.

If the worker is unreachable, submission returns `503` — grading never silently
falls back to something trusted.

## Tests

```bash
bun test                                        # unit suite: in-memory stores + FakeAssessClient
bunx tsc --noEmit                               # typecheck
DEVWORLD_IT=1 bun test tests/integration.test.ts  # live stores (needs infra/ up)
```

The unit suite covers every route, the progression equivalence, double-entry
validation, hidden-test secrecy, lock gates and reward-once semantics. The
integration suite asserts the real ClickHouse view derives a balance, that the
KV store persists, and that the full API flow works against live services.

## Configuration

See `.env.example`. Key values: `CLICKHOUSE_URL`, `KV_URL`, `REDIS_URL`,
`WORKER_URL` + `WORKER_API_KEY` (empty `WORKER_URL` disables grading), and
`CONTENT_DIR` (points at `game-shell/content`).
