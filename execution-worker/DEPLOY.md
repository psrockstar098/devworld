# Deploying the DEVWORLD execution worker (sandbox backend)

The worker is the sandbox that runs the 40 server-tier languages (C, C++,
C#, Java, Go, Rust, Zig, Kotlin, Swift, Dart, Julia, Scala, Haskell, plus
the wasm-tier languages when a backend is configured). It is a Bun +
TypeScript service with zero runtime dependencies beyond Docker.

## What is already verified (2026-10-22)

- `bun test`: 93 pass / 0 fail (456 assertions).
- Boots without Kvrocks/ClickHouse (in-memory submissions, no-op events)
  and without Docker — `/execute` then answers `execution backend
  unavailable` instead of crashing.
- Auth enforced: `/runtimes` without a key answers 401.

## What is NOT verified yet

Real Docker execution has never run: no Docker daemon exists on the dev VM.
The first real execution test happens on the deploy host (step 4 below).

## Deploy (any machine with Docker)

```sh
git clone https://github.com/psrockstar098/devworld
cd devworld/execution-worker
sudo mkdir -p /submissions   # or: export SUBMISSIONS_DIR=$HOME/devworld-submissions
export EXEC_API_KEY=<a long random key, yours only>
docker compose -f docker-compose.standalone.yml up -d --build
```

Pull the runtime images (18 runtimes: python, javascript, typescript, c,
cpp, csharp, java, go, rust, zig, kotlin, swift, dart, julia, scala,
haskell, sql, asm):

```sh
bun scripts/pull-runtimes.ts
```

## Verify on the host

```sh
curl http://localhost:8090/runtimes -H "x-api-key: $EXEC_API_KEY"
curl -X POST http://localhost:8090/execute -H "x-api-key: $EXEC_API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"runtime":"c","files":[{"path":"main.c","content":"#include <stdio.h>\nint main(){printf(\"hi\\n\");}"}]}'
```

Expect `{"run":{"stdout":"hi\n",...}}` (shape per `docs/api.md`).

## Connect the game

In the IDE: Settings → backend URL (`http://<host>:8090`) + the same API
key. The language badge flips from SANDBOX LATER to SANDBOX READY, and
Run on a server-tier language POSTs `/execute` with all workspace files.

For a public game backend, put the worker behind HTTPS (reverse proxy or
tunnel) and never ship the API key in the game client — players use their
own backend, or the game ships one default backend URL + key of its own.

## Notes

- Public Piston API is NOT an option: as of Feb 2026 it is whitelist-only
  (401). Self-hosting is the path.
- The full-stack `docker-compose.yml` in this directory is for the whole
  game (worker + Kvrocks + ClickHouse on an external `devworld` network);
  use `docker-compose.standalone.yml` when only the sandbox is needed.
