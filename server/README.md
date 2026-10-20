# DEVWORLD platform server (C++)

The authoritative DEVWORLD backend: a native C++17 HTTP + WebSocket server with
real persistence. It replaces the earlier TypeScript `game-server/` — that code
is kept only as a behavior reference.

No third-party libraries. SHA-256/SHA-1/HMAC/PBKDF2, HTTP/1.1, WebSocket
(RFC 6455), the RESP2 store client and the ClickHouse HTTP client are all in
`src/`.

```
browser (game shell)                 C++ server :8091
  fetch /auth/*  ────────────────►  api.cpp ── sessions (Redis)
  fetch /me, /missions             │        ── players  (Kvrocks)
  WebSocket /ws?token=…  ◄─────────┘        ── history  (ClickHouse)
```

| Tier | Store | What lives there |
|---|---|---|
| Main database | ClickHouse | append-only events, XP events, mission attempts, the double-entry ledger |
| Durable state | Kvrocks (RocksDB, RESP) | players, username/handle indexes, submissions |
| Cache / sessions | Redis | opaque session tokens (TTL), derived balance cache, pub/sub |

Level and balance are **derived**, never stored twice: XP is summed from
`xp_events`, balance from `ledger_entries` (every `ref` sums to zero), and only
the resulting scalar is cached briefly in Redis.

## Quick start

Stores (podman; Docker works the same):

```bash
podman start devworld-redis devworld-kv devworld-clickhouse
# first time only:
cd infra && podman compose up -d && ./scripts/healthcheck.sh
```

Build and run:

```bash
cd server
./build.sh
CONTENT_DIR=../game-shell/content PORT=8091 ./build/devworld-server
```

Or use the helper (starts stores, builds if needed, waits for health):

```bash
server/scripts/up.sh
server/scripts/stop.sh
```

Defaults target local infrastructure; every value is overridable (see
`.env.example`). The server refuses to start if it cannot load the mission
catalogue, but it will start with a store down and report it through `/health`
so a proxy can distinguish "process up" from "data plane down".

## Front-end

`game-shell/shell/shell.js` contains `DW.cloud`:

- account panel (pause menu → **Cloud account**, or the ☁ chip)
- register / login / logout against `/auth/*`
- profile, level, XP, credits, completed missions, appearance and Home Row unit
  from `/me`
- a live WebSocket at `/ws?token=…`; `mission.completed` updates the panel and
  the HUD immediately, while `world.snapshot`, `world.move`, `chat.message`,
  `player.updated` and friend events drive the shared town
- one shared exterior map per town/city; interiors stay private. The C++ shell
  draws the other accounts in the same map and publishes the local player's
  position on a heartbeat
- the in-game **NOVA-80 computer** launches the IDE, mission board, portfolio
  and mail from a desktop; the **phone** (`F`) is the social device for town
  chat, people/friends, profile, appearance, map and notifications
- backend URL is configurable in the panel and persisted in `localStorage`
- token is opaque and stored in `localStorage`; the browser never sees a
  password hash

Serve the shell on any static port (the server sends permissive CORS headers):

```bash
cd game-shell/dist && python3 -m http.server 8000
```

## API

All JSON. Authenticated routes accept `Authorization: Bearer <token>`.
`playerId` login is still accepted for old clients; new clients use
username/password.

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/` | – | service banner |
| GET | `/health` | – | process + all three stores |
| POST | `/auth/register` | – | `{username, password, displayName?}` → `{token, player}` |
| POST | `/auth/login` | – | `{username, password}` → `{token, player}` |
| POST | `/auth/logout` | bearer | revoke the session |
| GET | `/me` | bearer | player + derived progression + balance |
| GET | `/progression/me` | bearer | alias of `/me` |
| PUT | `/me/appearance` | bearer | update `skin`/`shirt`/`pants`/`hair`; returns the new player |
| GET | `/world/players` | bearer | who is currently in the shared world |
| GET | `/friends` | bearer | `{friends, incoming, outgoing}` |
| POST | `/friends/request` | bearer | `{id}` or `{handle}`; notifies the target over WS |
| POST | `/friends/accept` | bearer | `{id}`; both sides get `friend.accepted` |
| POST | `/friends/decline` | bearer | `{id}` |
| DELETE | `/friends/:id` | bearer | remove a friendship |
| GET | `/missions` | – | catalogue; never includes hidden tests |
| GET | `/stats/missions` | – | attempts/passes per mission |
| GET | `/players/:id` | – | public profile (no hash, includes look + Home Row unit) |
| GET | `/ledger/balance` | bearer | derived credit balance |
| POST | `/missions/:id/submit` | bearer | send files to the execution worker; records the attempt |
| POST | `/admin/ledger` | `X-Admin-Key` | post a balanced ledger transaction |
| GET | `/ws?token=…` | token | WebSocket: `hello`, `world.snapshot`, `world.move`, `chat.message`, `player.updated`, friend events, `mission.completed`, ping/pong |

Passwords: PBKDF2-HMAC-SHA256, 120,000 iterations, 16-byte salt, 32-byte hash,
serialized as `pbkdf2_sha256$<iters>$<salt-b64>$<hash-b64>`. Public player JSON
never includes `passwordHash`.

## Shared world protocol

Presence is ephemeral by design: the process registry holds the live map, Redis
mirrors it with a TTL, and `/world/players` reads the same entries. A position
heartbeat is published from the game loop even while a menu is open, so being
in the IDE or on the phone does not drop a player out of town. Interiors are
never published; only exterior maps are shared.

`world.state` (client → server) carries `{x, y, mapId}` and produces
`world.move` broadcasts. `chat.send` carries `{text}` and produces a
rate-limited `chat.message`. New connections receive a `world.snapshot` with
the current players and recent chat. Friend mutations push `friend.request`,
`friend.accepted`, `friend.declined` or `friend.removed`; appearance changes
push `player.updated`.

The first login assigns a Home Row unit with `INCR world:home:cursor`; existing
records without one are migrated lazily. Appearance is validated as four RGB
triples before it is written to the player record.

## Grading worker

`POST /missions/:id/submit` forwards the submitted files to an execution worker
(`WORKER_URL`, `WORKER_API_KEY`). The worker runs the visible tests **and** the
hidden tests and returns per-suite verdicts; this server never reads the hidden
tests and nothing in the API can change a verdict. With no worker configured the
route returns `503` rather than pretending to grade.

The education invariant from the game shell still holds: a pass on the shipped
tests alone is not mastery — mastery needs independent evidence plus working
code.

## Tests

```bash
server/tests/run_tests.sh     # 41 cases / ~300 assertions, real stores
python3 server/tests/e2e.py   # live end-to-end against :8091
```

Store-dependent cases self-skip when a store is absent; the end-to-end script
requires all three plus a running server. `e2e.py` covers auth, CORS, ledger
invariants, WebSocket framing, Kvrocks persistence and ClickHouse queries.

Browser test (headless Chrome, requires the static server on :8000 and the
server on :8091):

```bash
python3 -m http.server 8000 --directory game-shell/dist &
google-chrome --headless=new --remote-debugging-port=9222 \
  --user-data-dir=/tmp/devworld-chrome about:blank &
node server/tests/ui_test.mjs
```

It registers a user through the panel, checks the live socket and real
profile data, exercises sign-out and a wrong password, then signs back in.
Requires Node 22+ (global `fetch` and `WebSocket`) and a Chrome with remote
debugging on port 9222; set `CDP`, `PAGE` or `SERVER` to change the targets.

## Environment

| Variable | Default |
|---|---|
| `HOST` / `PORT` | `0.0.0.0` / `8091` |
| `CLICKHOUSE_URL` | `http://127.0.0.1:8123` |
| `CLICKHOUSE_USER` / `CLICKHOUSE_PASSWORD` | `devworld` / `devworld_dev_pw` |
| `CLICKHOUSE_DB` | `devworld` |
| `KV_URL` | `redis://:devworld_dev_pw@127.0.0.1:6666` |
| `REDIS_URL` | `redis://:devworld_dev_pw@127.0.0.1:6379` |
| `WORKER_URL` / `WORKER_API_KEY` | empty (grading disabled) |
| `ADMIN_KEY` | `dev_admin_key` |
| `CONTENT_DIR` | `../game-shell/content` |
| `SESSION_TTL_SEC` | `604800` (7 days) |
| `BALANCE_CACHE_TTL_SEC` | `15` |

## Notes

- CORS allows `Authorization`, `Content-Type`, `X-Admin-Key`; `OPTIONS` → 204.
- A `101` response is serialized without `Content-Length`/`keep-alive`; the
  upgrade handshake owns the socket afterwards.
- Build: `g++ 15`, C++17, `-O2 -pthread`. No CMake dependency: `build.sh` is a
  single compile line.
