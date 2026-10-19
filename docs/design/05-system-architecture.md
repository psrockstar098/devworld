# DEVWORLD — System Architecture

**Version:** 0.1 (2026-10-19)
**Scope:** backend services for the full game (Phase 1 builds: auth, player,
progression, execution/assessment; the rest are specified now, built later).
Must be consistent with what already exists: the embedded IDE (`window.GameIDE`
API), the execution worker (`POST /execute`, `POST /assess`, 18 runtimes, API
key + quotas — see `execution-worker/docs/api.md`), and the C++ → WASM game
shell (future).

---

## 0. Topology

```
                         ┌──────────────────────────────┐
                         │  Client (browser)            │
                         │  C++ shell (WASM) + VS Code  │
                         │  IDE (web) via GameIDE API   │
                         └──────────────┬───────────────┘
                                        │ HTTPS/WSS
                         ┌──────────────▼───────────────┐
                         │  API gateway (auth, limits)  │
                         └──┬──┬──┬──┬──┬──┬──┬──┬──┬──┘
                            │  │  │  │  │  │  │  │  │
              ┌─────────────┘  │  │  │  │  │  │  │  └─────────────┐
              ▼                ▼  ▼  ▼  ▼  ▼  ▼  ▼                ▼
         ┌────────┐ ┌──────┐ ┌───┐ ┌────┐ ┌───┐ ┌────┐ ┌───┐ ┌────────┐ ┌───────┐
         │  §1 Auth│ │§2 Prog│ │§3 │ │ §4 │ │§5 │ │ §6 │ │§7 │ │  §8 AI │ │§9 Ops │
         │+player │ │+learn│ │Exec│ │ RT │ │World│ │Proj│ │Co │ │       │ │      │
         └────────┘ └──────┘ └───┘ └────┘ └───┘ └────┘ └───┘ └────────┘ └───────┘
              │         │      │     │     │      │     │        │         │
              └─────────┴──────┴─────┴─────┴──────┴─────┴────────┴─────────┘
                                        │
                              ┌─────────▼─────────┐
                              │  Postgres (system │
                              │  of record) +     │
                              │  Redis (cache/RT) │
                              │  + object storage │
                              └───────────────────┘
```

Service boundaries are hard: no service executes player code except §3, and
§3 never touches player PII. Cross-service calls go through the gateway with
service credentials; the client never calls §3–§9 directly except via
gateway-pathed routes.

---

## §1 — Authentication + player services

Owns: identity, sessions, profiles ([GDD] §1).
- OAuth (Google/GitHub) + passkey; session cookies httpOnly + SameSite.
- `POST /auth/login`, `POST /auth/refresh`, `POST /auth/logout`.
- `POST /players`, `PATCH /players/me`, `GET /players/{handle}` (public subset, no PII).
- Data: `players`, `sessions`, `achievements` (see [GDD] §1).
- Security boundary: the only service that sees credentials; issues
  short-lived JWTs (15 min) + refresh rotation. Rate-limit login 10/min/IP.

## §2 — Progression + learning services

Owns: XP/levels, skill trees, missions, assessments, paths ([GDD] §2, §3, §4, §9).
- `GET /progression/me`, `POST /progression/assess` (gate attempts),
  `GET /skills/me`, `GET /missions`, `POST /missions/{id}/submit`,
  `POST /missions/{id}/hint`, `GET /paths`, `POST /paths/select`.
- Submitting a mission internally calls §3 `/assess` (never exposes hidden
  tests to the client; only verdicts + safe diagnostics return).
- XP/level math is server-side and append-only; content (missions, trees,
  levels) is versioned static data, content-hashed.
- Security boundary: hidden test content lives here and in §3 only.

## §3 — Code execution + assessment infrastructure

Owns: running untrusted code ([GDD] §4 platform, §9, §11).
- **Already built:** `execution-worker/` — `POST /execute`, `POST /assess`,
  `GET /runtimes`; 18 Docker-sandboxed runtimes; API-key auth; per-key quotas;
  threat model in `execution-worker/docs/threat-model.md`. This service IS that
  worker, promoted to a first-class service.
- Additions for the game: mission-scoped execution policies (per-mission
  timeout/memory overrides), deterministic image pins (reproducible grading),
  forensics hooks (timing/plagiarism signals → §9 moderation feed).
- Security boundary: the ONLY service that runs player code. No network
  egress, no PII ingress, no access to other services' databases. Runs on
  dedicated hosts (docker.sock = host root — never co-located with §1/§2).

## §4 — Multiplayer + realtime communication

Owns: presence, chat, guilds/teams channels, collaborative mission sync
([GDD] §8). Phase 2+.
- WebSocket (presence, chat, mission sync) + SSE fallback; Redis pub/sub
  between gateway instances.
- Channels: `presence:{player}`, `chat:{guild|team|friends}`, `mission:{instance}`.
- Security boundary: every message filtered (§9 moderation); no DMs from
  strangers; history TTL'd, never authoritative for grades.

## §5 — World simulation + economy

Owns: map/cities/gating, travel, wallets, ledger, inventory, housing, jobs
feed ([GDD] §5, §6, §7-jobs).
- `POST /world/travel` (idempotency-keyed), `GET /world/map`,
  `POST /shop/buy` (idempotency-keyed), `GET /wallet`, `GET /inventory`,
  `GET /jobs/feed`, `POST /jobs/{id}/apply`, `POST /jobs/{id}/submit`.
- Ledger is double-entry; balances derived. Gating evaluation is pure and
  auditable (same inputs → same unlocks).
- Security boundary: the only writer to the ledger; all money movements
  signed with idempotency keys; anomaly detection hooks → moderation.

## §6 — Project hosting + artifact storage

Owns: player projects, content-addressed file storage, marketplace listings
([GDD] §10).
- `POST /projects`, `PUT /projects/{id}/files` (content-addressed),
  `POST /projects/{id}/publish`, `POST /listings/{id}/buy`.
- Object storage (S3-compatible) for blobs; Postgres for metadata.
- Pre-publish scan pipeline (malware/secret scan) as an async job queue.
- Security boundary: uploads are untrusted until scanned; published code is
  source-only (no executable downloads); demo runs go through §3 read-only.

## §7 — Company + contract management

Owns: companies, employees, contracts, escrow (Phase 2+, [GDD] §7).
- `POST /companies`, `POST /companies/{id}/hire`, `POST /contracts`,
  `POST /contracts/{id}/milestones/{n}/submit`, escrow release on verified
  milestones (calls §2 assessment internally).
- Company ledger separate from personal wallets; bankruptcy dissolves
  without touching progression.
- Security boundary: contract terms hash-pinned at acceptance; payouts via
  §5 ledger writer only.

## §8 — AI orchestration + safety controls

Owns: the in-game AI mentor ([GDD] §12).
- `POST /ai/ask` — mode-pinned (learning/debugging/review/professional;
  independent-challenge refuses), context-scoped to current mission/file.
- Guardrails: solution-shape detection in locked modes, PII redaction,
  per-player rate limits, full audit log (hashes; text per retention policy).
- Security boundary: the AI service never sees hidden tests, never sees
  other players' data, and cannot write to progression directly.

## §9 — Analytics, observability, administration

Owns: events, metrics, logs, traces, admin tools, moderation tooling
(supporting [GDD] §13).
- Event ingestion (`POST /events/batch`, validated against [§11 schemas]),
  metrics (Prometheus-style), structured logs, OpenTelemetry traces.
- Admin: role-gated console (content, moderation, support); every admin
  action immutably logged.
- Security boundary: analytics never stores PII beyond the data-retention
  policy; admin access requires MFA + audit.

---

## §10 — Shared design language

- **IDs:** ULID strings, lexicographically sortable (`01J...`).
- **Time:** UTC ISO-8601 everywhere; game-time vs wall-time explicitly named.
- **Money:** integer minor units (coins have no subdivisions in Phase 1);
  currency code field reserved (`DEV`).
- **Errors:** `{ "error": { "code": "MACHINE_CODE", "message": "human text", "retryable": bool } }`.
- **Pagination:** cursor-based (`?cursor=&limit=`), never OFFSET.
- **Versioning:** `/v1/` prefix; breaking changes → new version, 6-month sunset.
- **Idempotency:** `Idempotency-Key` header on all mutating calls; 24h window.

## §11 — API contracts (REST conventions)

Shapes follow the existing execution-worker conventions:

```jsonc
// POST /v1/missions/{id}/submit  (via gateway → §2 → §3)
{ "files": [{ "path": "main.py", "content": "..." }], "timeout": 30 }
// 200 → { "verdict": "pass|fail",
//         "public": [{ "name": "...", "passed": true, "actual": "..." }],
//         "hidden": { "passed": 3, "total": 5 },   // counts only, never content
//         "diagnostics": "...", "xp_awarded": 120 }
```

```jsonc
// POST /v1/ai/ask  (§8)
{ "mode": "debugging", "mission_id": "01J...", "code": "...", "question": "..." }
// 200 → { "reply": "...", "mode": "debugging", "solution_blocked": false }
```

All contracts are additive — new fields are optional; clients ignore unknown fields.

## §12 — Event schemas

Base envelope: `{ "event": "snake.case.name", "v": 1, "at": "ISO-8601",
"player_id": "ULID|null", "props": {…} }`. Core events (non-exhaustive):
`player.signup`, `mission.start/submit/pass/fail`, `assessment.gate_pass`,
`level.up`, `skill_node.mastered`, `job.applied/completed`, `wallet.credited/debited`,
`ai.asked`, `ai.solution_blocked`, `moderation.flagged/actioned`,
`cheat.signal`. Full registry lives with the analytics service; new events
require a schema entry before emission.

## §13 — Database conventions

- Postgres is the system of record for all services except §3 (stateless)
  and §4 (Redis-backed ephemeral).
- Naming: `snake_case` tables, `<domain>_<entity>`; every table has
  `id ULID PK`, `created_at`, `updated_at`.
- Money tables are append-only (no UPDATE/DELETE on ledger rows).
- Migrations are forward-only, reviewed, and reversible in staging first.
- Per-service schemas (no cross-service table reads; cross-service needs go
  through APIs).

## §14 — Initial data model (core tables)

```
players(id, handle, avatar_json, level, title, reputation, status, created_at)
player_levels(player_id, level, xp, updated_at)
level_definitions(level, xp_required, gate_assessment_id, unlocks_json, version)
skill_nodes_def(tree_id, node_id, requires_json, evidence_bar_json, version)
player_skill_nodes(player_id, tree_id, node_id, state, evidence_ref)
language_paths(language, tier, runner_kind, ls_status, mission_count)
player_paths(player_id, language, primary, started_at)
missions(id, version, tier, path, content_hash, spec_json)
mission_attempts(id, player_id, mission_id, files_hash, verdict, at)
assessment_results(id, player_id, ref_kind, ref_id, passed, evidence_hash, at)
cities(id, name), districts(id, city_id, name)
player_location(player_id, city_id, district_id)
unlocked_cities(player_id, city_id, unlocked_at)
wallets(player_id, balance_cached)          -- derived; ledger is truth
ledger(id, player_id, kind, amount, ref_kind, ref_id, idempotency_key, at)
inventory(player_id, item_id, qty)
items(id, name, kind, stats_json, price)
housing(player_id, property_id, rent_due_at)
job_defs(id, ...), job_instances(id, player_id, job_id, state, at)
projects(id, player_id, name), project_files(project_id, path, hash)
listings(id, project_id, price, state), sales(id, listing_id, buyer_id, at)
ai_sessions(id, player_id, mode, mission_id), ai_events(id, session_id, hash, at)
reports(id, reporter_id, target_kind, target_id, reason, state)
moderation_actions(id, target_id, kind, reason, actor_id, at, reversible)
```

## OPEN

- **OPEN-A1 — Hosting topology.** Single-region vs multi-region, and the
  execution-host fleet sizing: closes after Phase 1 load testing.
- **OPEN-A2 — Realtime vendor vs self-hosted.** Build on raw WebSocket +
  Redis vs. a managed realtime service: closes in Phase 2 planning.
- **OPEN-A3 — Data retention policy.** Exact TTLs for chat, AI text, forensics:
  closes with [PRD] OPEN-3 (legal review).
