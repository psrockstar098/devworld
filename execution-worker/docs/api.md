# Execution Worker API

Base URL is configurable (default `http://localhost:8090`). Every endpoint
except `GET /health` requires the `x-api-key` header (an
`Authorization: Bearer <key>` header is also accepted).

## Endpoints

### GET /health

No auth. Returns `{ "ok": true }`. Used by the container healthcheck.

### GET /runtimes

Returns the runtime registry:

```json
{ "runtimes": [ { "id": "python", "label": "Python", "version": "3.14", ... } ] }
```

### POST /execute

Run one submission in a fresh container.

```bash
curl -s http://localhost:8090/execute \
  -H "x-api-key: $EXEC_API_KEY" -H "Content-Type: application/json" \
  -d '{
    "runtime": "python",
    "files": [{ "path": "main.py", "content": "name = input().strip()\nprint(f\"hello {name}\")" }],
    "stdin": "dominic\n",
    "timeout": 10
  }'
```

Request fields:

| Field     | Type               | Required | Notes                                                        |
|-----------|--------------------|----------|--------------------------------------------------------------|
| `runtime`      | string             | yes      | Runtime id from `GET /runtimes`                          |
| `files`        | [{path, content}]  | yes      | 1-20 files, relative paths, 200 KB max each, 1 MB total max |
| `stdin`        | string             | no       | Piped to the program's stdin, 64 KB max                  |
| `timeout`      | number (seconds)   | no       | 1-120; defaults to the runtime's `timeoutSec`            |
| `dependencies` | [string]           | no       | Approved package names (see *Dependency policy*), max 10  |
| `submissionId` | string             | no       | Attach this run as a new revision of a stored submission |

Entry file: the file matching the runtime's `entryFile` (e.g. `main.py`,
`Main.java`) is used as the program entry; otherwise the first file in
`files` is used.

Response (200):

```json
{
  "runtime": "python",
  "stdout": "hello dominic\n",
  "stderr": "",
  "exitCode": 0,
  "timedOut": false,
  "truncated": false,
  "durationMs": 231,
  "artifacts": [{ "path": "chart.png", "base64": "..." }],
  "revision": 3
}
```

`artifacts` are files the program wrote to `/work/output`, base64-encoded
(5 MB total cap). `truncated` is true when stdout/stderr exceeded the
256 KB cap each. `revision` is present only when `submissionId` was sent, and
is the number of the revision the run was saved as.

### POST /assess

Run one submission against public and hidden tests. Each test runs in a
FRESH container with the test's `stdin`.

```bash
curl -s http://localhost:8090/assess \
  -H "x-api-key: $EXEC_API_KEY" -H "Content-Type: application/json" \
  -d '{
    "runtime": "python",
    "files": [{ "path": "main.py", "content": "a,b=map(int,input().split());print(a+b)" }],
    "tests": [
      { "name": "adds positives", "stdin": "2 3\n", "expected_stdout": "5\n", "hidden": false },
      { "name": "handles zero",   "stdin": "0 0\n", "expected_stdout": "0\n", "hidden": true  }
    ]
  }'
```

Test fields: `name` (1-100 chars), `stdin` (64 KB max), `expected_stdout`
(64 KB max), `hidden` (boolean, default false). Max 20 tests per request.
`/assess` also accepts the optional `dependencies` and `submissionId` fields
from `/execute`; when `submissionId` is set, the files are stored as a new
revision and its verdict is recorded (see *Submissions*).

Output comparison trims trailing whitespace per line, normalizes line
endings, and ignores leading/trailing blank lines; anything else must match
exactly.

Response (200):

```json
{
  "runtime": "python",
  "passed": 1,
  "total": 2,
  "revision": 3,
  "results": [
    { "name": "adds positives", "passed": true, "hidden": false, "actual": "5" },
    { "name": "handles zero", "passed": false, "hidden": true, "errorKind": "wrong-output" }
  ]
}
```

`revision` is present only when `submissionId` was sent.

Per-test result fields:

| Field       | Present when            | Meaning                                              |
|-------------|-------------------------|------------------------------------------------------|
| `name`      | always                  | Test name                                            |
| `passed`    | always                  | Whether the test passed                              |
| `hidden`    | always                  | Whether the test is hidden                           |
| `actual`    | non-hidden only         | Program's normalized stdout                          |
| `errorKind` | failed tests only       | `wrong-output`, `runtime-error`, `timeout`, or `build-failed` |
| `error`     | failed, non-hidden only | Human-readable diagnostics (e.g. compiler output)    |

**Hidden-test guarantee:** hidden tests never return `actual`, `error`, or
anything containing the hidden stdin/expected output — only `passed` and a
data-free `errorKind`.

## Dependency policy

Player code never reaches the network, so a submission cannot `pip install` or
`npm install`. A submission may instead **request** dependencies by name; the
worker resolves each against the allowlist in `dependencies.json`:

- not on the allowlist → `403 dependency not approved`
- on the allowlist but not baked into the image yet → `400 ... needs a
  controlled mirror`
- on the allowlist and baked in → allowed; the runtime image already has it

Versions are always pinned (`.env`-style ranges are rejected at load time) so
grading is reproducible. `GET /dependencies` returns the list.

### GET /dependencies

Optional `?runtime=<id>` filters to one runtime.

```json
{ "approved": [ { "runtime": "typescript", "name": "typescript", "version": "5.9", "baked": true } ] }
```

## Submissions

A **submission** is a project that survives across revisions. Each revision is
content-hashed and can carry the verdict of the assessment that produced it, so
a player (and moderation) can see exactly what changed between two attempts and
whether it helped.

Revisions are stored in memory by default (bounded to the 500 most recent), or
durably in **RocksDB/Kvrocks** when `KV_URL` is set — both behind the
`SubmissionStore` interface, so an object-storage implementation drops in
without touching the HTTP layer. The KV store keeps a recency index
(`submissions:created`) and evicts the oldest document when it exceeds the cap.

Assessment outcomes and moderation signals are appended to **ClickHouse** when
`CLICKHOUSE_URL` is set (`assessment_events`, `moderation_signals`). This is
best-effort telemetry and never affects a response.

### POST /submissions

Store a project's first revision. Same body as `/execute` (no `tests`, no run).

```bash
curl -s http://localhost:8090/submissions \
  -H "x-api-key: $EXEC_API_KEY" -H "Content-Type: application/json" \
  -d '{ "runtime": "python",
        "files": [{ "path": "main.py", "content": "print(1)" }],
        "dependencies": [] }'
```

```json
{
  "id": "3f0c…",
  "runtime": "python",
  "dependencies": [],
  "created": "2026-10-19T10:00:00.000Z",
  "revisions": [ { "n": 1, "at": "…", "hash": "9a1c…", "files": ["main.py"], "verdict": null } ]
}
```

### GET /submissions/:id

The same metadata with every revision's number, timestamp, content hash, file
list and verdict.

### GET /submissions/:id/revisions/:n

One revision in full: `{ n, at, hash, files: [{ path, content }], verdict }`.

### GET /submissions/:id/compare?from=1&to=2

Line-level diff between two revisions plus the verdict delta.

```json
{
  "from": 1, "to": 2,
  "files": [ { "path": "main.py", "status": "changed", "added": 1, "removed": 1,
               "lines": [ { "kind": "del", "text": "print(1)" },
                          { "kind": "add", "text": "print(2)" } ] } ],
  "added": 1, "removed": 1,
  "verdict": { "before": { "passed": 1, "total": 2 },
               "after":  { "passed": 2, "total": 2 }, "deltaPassed": 1 }
}
```

`status` is `added`, `removed`, `changed` or `unchanged`. Files are matched by
path, not order. Diffs over 1,200 lines per file fall back to a coarse
whole-file replace.

## Moderation

### GET /moderation/signals

Advisory abuse signals for a moderation feed. Quotas and the concurrency limit
keep the service up; these signals flag patterns without blocking on their own.
Kinds: `request-burst`, `payload-fixture`, `duplicate-submission`,
`hidden-tail` (repeatedly failing **only** the hidden tests — possible
brute-forcing). Thresholds live in `src/abuse.ts` and are injectable for tests.

```json
{ "signals": [ { "key": "…", "kind": "duplicate-submission",
                 "detail": "6 identical submissions (9a1c…)", "at": "…", "count": 6 } ] }
```

## Error codes

| Status | Meaning                                                        |
|--------|----------------------------------------------------------------|
| 400    | Bad request: unknown runtime, invalid files/tests/timeout, or a dependency that is approved but not baked in yet |
| 401    | Missing or invalid `x-api-key`                                 |
| 403    | Dependency not on the allowlist                                |
| 404    | Unknown route, submission or revision                          |
| 413    | Payload too large (file, stdin, or total submission cap)       |
| 429    | Hourly quota exceeded; `retryAfterSec` in body, `Retry-After` header |
| 502    | Execution backend unavailable (Docker daemon error)            |
| 503    | Server busy: concurrency limit reached                         |

## Runtime table

| id         | Language              | Version        | Docker image                          | Custom build | Timeout | Memory |
|------------|-----------------------|----------------|---------------------------------------|--------------|---------|--------|
| python     | Python                | 3.14           | `python:3.14-slim`                    | no           | 10s     | 256m   |
| javascript | JavaScript (Node.js)  | 22             | `node:22-slim`                        | no           | 10s     | 256m   |
| typescript | TypeScript            | 5.9            | `devworld/runtime-typescript:5.9`     | yes          | 30s     | 256m   |
| c          | C (GCC)               | 14             | `gcc:14`                              | no           | 30s     | 256m   |
| cpp        | C++ (G++)             | 14             | `gcc:14`                              | no           | 30s     | 256m   |
| java       | Java                  | 21             | `eclipse-temurin:21-jdk-alpine`       | no           | 30s     | 512m   |
| csharp     | C# (.NET)             | 9              | `devworld/runtime-csharp:9.0`         | yes          | 60s     | 512m   |
| go         | Go                    | 1.24           | `golang:1.24-alpine`                  | no           | 30s     | 256m   |
| rust       | Rust                  | 1.84           | `rust:1.84-slim`                      | no           | 60s     | 512m   |
| sql        | SQL (SQLite)          | 3 (CLI)        | `devworld/runtime-sqlite:3`           | yes          | 10s     | 128m   |
| swift      | Swift                 | 6.1            | `swift:6.1-jammy`                     | no           | 90s     | 1g     |
| kotlin     | Kotlin                | 2.2            | `devworld/runtime-kotlin:2.2`         | yes          | 90s     | 1g     |
| dart       | Dart                  | 3.9            | `dart:3.9-sdk`                        | no           | 30s     | 512m   |
| julia      | Julia                 | 1.11           | `julia:1.11`                          | no           | 120s    | 1g     |
| scala      | Scala                 | 3.7            | `devworld/runtime-scala:3.7`          | yes          | 120s    | 1g     |
| haskell    | Haskell (GHC)         | 9.8            | `haskell:9.8-slim`                    | no           | 120s    | 1g     |
| zig        | Zig                   | 0.15.1         | `devworld/runtime-zig:0.15.1`         | yes          | 60s     | 512m   |
| asm        | x86-64 Assembly (NASM)| 2.16           | `devworld/runtime-nasm:2.16`          | yes          | 30s     | 128m   |

Custom-build images have a Dockerfile under `images/` (built with
`bun run pull-runtimes`): TypeScript bakes in `typescript@5.9`; C# wraps the
SDK's Roslyn `csc.dll` in a `csc-shim` (offline `dotnet run` restore is
impossible without network); SQLite is alpine + sqlite3 CLI; Kotlin installs
the JetBrains compiler release; Scala installs via Coursier; Zig installs the
upstream tarball; NASM is alpine + nasm/binutils.

All runtimes run with `--network none`, 0.5-1.0 CPUs, and a 64-128 process
limit. Conventions: compiled languages build every source file at the
project root (except Rust, Haskell, Zig, and NASM, which compile the entry
file); Java's entry class name must match its file name; Scala's entry object
is named after the entry file; Assembly programs use raw Linux syscalls with
a `_start` entry point (no libc).
