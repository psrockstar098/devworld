# DEVWORLD Execution Worker

Sandboxed code-execution backend for the DEVWORLD game. Runs player code for
18 languages in locked-down Docker containers and grades missions through a
hidden-test assessment API. The browser IDE calls this service for every
language that cannot run locally (C, C++, Java, C#, Go, Rust, Swift, Kotlin,
Dart, Julia, Scala, Haskell, Zig, x86-64 Assembly, TypeScript, SQL).

Beyond single runs it stores **submissions** (projects across revisions, each
with a content hash and verdict, diffable revision-to-revision), enforces an
**approved-dependency allowlist** (player code has no network), and exposes
advisory **abuse signals** for moderation.

Submission documents persist to the shared **RocksDB** store (Kvrocks, `KV_URL`)
behind the `SubmissionStore` interface — the default stays in-memory. Grading
outcomes and moderation signals are written to **ClickHouse** (`CLICKHOUSE_URL`)
best-effort, so a slow or down analytics store can never fail a grade. Both are
optional; the worker runs standalone without them.

## One-command local run

```bash
cd execution-worker
cp .env.example .env
# edit .env: set EXEC_API_KEY to a long random string
sudo mkdir -p /submissions          # must exist on the HOST (see note below)
bun run pull-runtimes               # pull/build all 18 runtime images (once)
docker compose up --build -d
curl http://localhost:8090/health
```

**`/submissions` note:** the worker spawns sibling containers through the
host Docker daemon, so bind-mount paths are resolved on the host. The
submissions directory must be mounted at the *same absolute path* inside the
worker container and on the host — `docker-compose.yml` maps
`${SUBMISSIONS_DIR:-/submissions}` to `/submissions`. Create it on the host
first, or set `SUBMISSIONS_DIR` to another host path.

Without Docker (dev loop):

```bash
bun install
EXEC_API_KEY=dev bun server.ts     # serves the API; /execute needs Docker
bun test                           # 93 tests, no Docker required
```

## How the game frontend connects

Settings in the game client needs two values:

- **Backend URL** — e.g. `http://localhost:8090` (dev) or your deployed host.
- **API key** — the `EXEC_API_KEY` value, sent as the `x-api-key` header.

Flow:

1. `GET /runtimes` to populate the language picker (id, label, version).
2. `POST /execute` with `{ runtime, files, stdin?, timeout? }` for free-run
   code execution; render `stdout`/`stderr`/`exitCode`/`timedOut`.
3. `POST /assess` with `{ runtime, files, tests }` to grade a mission;
   `tests[].hidden` marks hidden tests whose data is never echoed back.
4. `POST /submissions` once per project, then pass its `id` as `submissionId`
   to `/assess` to save each attempt as a revision; `GET
   /submissions/:id/compare?from=&to=` shows what changed and whether it
   helped.
5. `GET /dependencies` before offering a package install; request names in the
   `dependencies` field — anything off the allowlist is refused.

Full request/response shapes, the runtime table, and error codes:
[docs/api.md](docs/api.md). Threat model and isolation details:
[docs/threat-model.md](docs/threat-model.md).

## Layout

```
server.ts            # entrypoint: env config + Bun.serve
runtimes.json        # runtime registry (18 languages)
dependencies.json    # approved-dependency allowlist
src/
  app.ts             # routes, auth, quota, assessment orchestration
  runner.ts          # ContainerRunner interface + Docker CLI backend + FakeRunner
  runtimes.ts        # registry loader/validator
  validate.ts        # request validation (sizes, paths, tests)
  compare.ts         # output normalization/comparison
  quota.ts           # per-key token bucket
  semaphore.ts       # concurrency bound
  deps.ts            # approved-dependency policy (allowlist + bake state)
  submissions.ts     # revision store interface + line-level diff
  submissions.kv.ts  # RocksDB (Kvrocks) submission store
  kv.ts              # minimal KV client (RedisKv + MemoryKv)
  clickhouse.ts      # minimal ClickHouse HTTP client
  history.ts         # ClickHouse event sink (assessments + moderation signals)
  abuse.ts           # advisory abuse-signal detector
tests/               # bun test, Docker-free via FakeRunner
images/              # Dockerfiles for the 7 custom runtime images
scripts/pull-runtimes.ts  # provision all runtime images on a host
docs/api.md docs/threat-model.md
Dockerfile docker-compose.yml .env.example
```

## Configuration

| Variable | Default | Meaning |
|----------|---------|---------|
| `EXEC_API_KEY` | (required) | API key for `x-api-key` auth |
| `PORT` | 8090 | Listen port |
| `SUBMISSIONS_DIR` | /submissions | Scratch dir, must be 1:1 host bind |
| `EXEC_QUOTA_PER_HOUR` | 60 | Executions per key per hour |
| `EXEC_MAX_CONCURRENT` | 4 | Max simultaneous containers |
| `EXEC_MAX_REQUEST_BYTES` | 1048576 | Max submission bytes per request |
| `KV_URL` | (unset) | RocksDB (Kvrocks) URL; unset keeps submissions in memory |
| `CLICKHOUSE_URL` | (unset) | ClickHouse URL for the event sink; unset disables it |
| `CLICKHOUSE_DB` / `CLICKHOUSE_USER` / `CLICKHOUSE_PASSWORD` | devworld / devworld / devworld_dev_pw | ClickHouse auth |
