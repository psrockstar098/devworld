# Threat model: sandboxed code execution

## Scope

The worker executes **arbitrary untrusted code** submitted by game players:
infinite loops, fork bombs, filesystem abuse, attempts to escape the sandbox,
and attempts to attack other players' runs or the host. The design assumes
the submitter is adversarial.

## Isolation layers

Every execution gets a **fresh container** (`docker run --rm`) with:

| Flag | Purpose |
|------|---------|
| `--network none` | No network: no exfiltration, no downloading payloads, no attacking other hosts |
| `--read-only` | Root filesystem is read-only; malware cannot persist or modify the image |
| `-v <dir>:/work:rw` | Exactly one writable bind: the per-run submission dir. Nothing else on the host is visible |
| `--tmpfs /tmp:rw,size=64m,noexec` | Small writable temp for compilers; `noexec` blocks running binaries from /tmp |
| `--cap-drop ALL` | Drops every Linux capability, including the in-container-root ones that matter |
| `--security-opt no-new-privileges=true` | Blocks setuid escalation inside the container |
| `--pids-limit 64/128` | Fork bombs die at the process limit |
| `--memory 128m-1g` + `--memory-swap` equal | OOM killer caps memory hogs; swap disabled so limits are real |
| `--cpus 0.5-1.0` | CPU hogs are throttled, not just killed |
| `--init` | Reaps zombie processes |
| wall-clock timeout + `docker kill` | Infinite loops and sleeps are killed; `timedOut: true` is reported |

Per-runtime resource limits:

| Runtime(s) | Timeout | Memory | CPUs | PIDs |
|------------|---------|--------|------|------|
| python, javascript, sql | 10s | 128-256m | 0.5 | 64 |
| typescript, c, cpp, go, asm | 30s | 128-256m | 0.5-1.0 | 64 |
| csharp, rust, dart, zig | 30-60s | 256-512m | 0.5-1.0 | 64 |
| java, swift, kotlin | 30-90s | 512m-1g | 1.0 | 64-128 |
| julia, scala, haskell | 120s | 1g | 1.0 | 128 |

## Filesystem restrictions

- Submission files are written to a unique host dir per run and mounted at
  `/work` (rw). Path traversal (`..`, absolute paths, reserved `__` prefix)
  is rejected at validation time.
- Program output is captured from files inside `/work`, not from the Docker
  attach stream, so stdout/stderr stay separable and truncatable.
- stdout/stderr are truncated to 256 KB each (pipes keep draining so a noisy
  process cannot deadlock).
- Artifacts are collected only from `/work/output`, symlinks are never
  followed, 5 MB total cap.
- The scratch dir is deleted after every run (`finally` block), even on
  timeouts and crashes.

## Abuse detection and rate control

- **API key auth** (`EXEC_API_KEY`): missing/invalid key gets 401 on every
  route except `/health`.
- **Token bucket quota**: 60 executions/hour per key by default
  (`EXEC_QUOTA_PER_HOUR`), 429 with `Retry-After` when exhausted.
- **Concurrency semaphore**: max 4 simultaneous containers by default
  (`EXEC_MAX_CONCURRENT`); excess requests fail fast with 503 instead of
  queueing unboundedly.
- **Request size caps**: 200 KB per file, 1 MB total submission, 64 KB stdin,
  20 files / 20 tests max; oversized bodies get 413 before parsing.
- Quotas and semaphores are per worker process (in-memory); run one worker
  replica or put a shared rate limiter in front for multi-replica setups.

## What this does NOT protect against

- **Kernel exploits / container escapes.** Docker's default isolation (even
  hardened as above) shares the host kernel. A kernel-level exploit in
  player-submitted code could escape. Higher-risk workloads — OS kernels,
  raw untrusted binaries, deliberately adversarial exploit code — need
  stronger isolation: **gVisor (runsc)** or **Firecracker microVMs** in place
  of (or in front of) plain `docker run`. The runner is behind the
  `ContainerRunner` interface precisely so the backend can be swapped.
- **Side-channel attacks** between concurrent runs on shared hardware
  (timing, cache). Mitigation: keep concurrency low, use dedicated hosts.
- **Malicious or trojaned images.** Only pinned official/maintained base
  images and the Dockerfiles in `images/` are used; custom images are built
  from those Dockerfiles, never pulled blindly. Rebuild and repull
  periodically for CVE fixes.
- **Algorithmic DoS via expensive compiles** (e.g. template-heavy C++).
  Partially mitigated by quotas, timeouts, and CPU/memory caps.
- **The docker.sock mount.** The worker container mounts the host Docker
  socket, which is equivalent to host root access for anyone who can command
  the worker. Protect the worker host/network accordingly; the API key is
  the only gate.

## Separation of assessment infrastructure from production

- The worker is **stateless**: no player data, scores, or submissions persist
  past a run (scratch dirs are deleted; quotas live in memory).
- Run the worker on **dedicated host(s)**, not on the host serving the game
  API, database, or player accounts. A separate Docker context / project
  keeps assessment containers away from production containers.
- Hidden test data lives in the **game server**, never in the worker: the
  worker only sees the stdin/expected output it is asked to check, and its
  assess responses provably exclude hidden data (covered by tests).
- Rotate `EXEC_API_KEY` independently of other credentials; keep `.env`
  out of version control.
