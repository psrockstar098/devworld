# DEVWORLD — Game Design Document

**Version:** 0.1 (design-lock draft, 2026-10-19)
**How to read:** Every system below follows the same seven-part format:
purpose + UX · inputs/outputs/state changes · dependencies · rules and failure
cases · data model + API · security · testing. Cross-references: [PRD]
`01-product-requirements.md`, [WORLD] `03-world-specification.md`, [PROG]
`04-progression-50-levels.md`, [ARCH] `05-system-architecture.md`, [TEST]
`06-testing-strategy.md`.

Global conventions: all player-facing text is honest about what was tested
([PRD] §6); no system may present simulated outcomes as real-world
credentials; every system logs analytics events per [ARCH] §10.

---

## §1 — Player identity, profiles, character customization, reputation

**Purpose + UX.** The player exists in the world as a persistent character with
a public profile. Customization (appearance, handle, title) is the social
surface; the profile is the skill surface — it shows verified achievements,
not self-reported claims. First session: pick a handle, customize a cozy-2D
avatar, receive a starter room and computer.

**Inputs / outputs / state changes.**
- In: customization choices, handle (unique), avatar options; profile views by others.
- Out: rendered avatar, public profile page (handle, level, title, verified
  achievements, reputation score, company affiliation).
- State: `players` record created on signup; customization and reputation are
  mutable; handle immutable after creation (rename token = paid/voucher item, Phase 2).

**Dependencies.** Progression (§2) writes achievements; Reputation reads job
completion quality (§7) and peer reviews (§8); moderation (§13) can flag/suspend.

**Rules and failure cases.**
- Handles: 3–16 chars, unique, profanity-filtered; impersonation of staff
  handles is blocked by reserved list.
- Reputation is earned only from completed work (jobs, contracts, reviews);
  it decays slowly with inactivity (Phase 2+) to prevent stale-signal hoarding.
- Failure: duplicate handle → guided rename; filtered name → explain which
  rule, offer alternatives; never silently rename.

**Data model + API.** `players(id, handle, avatar_json, level, title, reputation,
created_at, status)`; `achievements(player_id, kind, ref_id, verified_at, proof_hash)`.
REST: `POST /players`, `PATCH /players/me`, `GET /players/{handle}` (public subset).
[ARCH] §1, §5.

**Security.** Auth via [ARCH] §1 (OAuth + passkey where available); profile
writes require session ownership; public profile exposes no email/IP;
avatar assets are allowlisted (no arbitrary uploads in Phase 1).

**Testing.** Handle validation fuzz tests; reputation math unit tests
(earn/decay boundaries); profile privacy tests (no PII leak). [TEST] §2.

---

## §2 — 50-level progression system

**Purpose + UX.** The spine of the game: 50 levels from Beginner to Creator
([PROG] for the full table). Levels are *broad progression* — they gate the
world (cities, jobs, equipment). Skill trees (§3) track *per-technology
mastery*. Level-ups feel earned: each level ends with an independent
assessment, and the world visibly changes (new districts, better jobs).

**Inputs / outputs / state changes.**
- In: XP (from missions, jobs, assessments), independent assessment results.
- Out: level-up event, unlocks (locations, jobs, equipment, tech), title change.
- State: `player_levels(player_id, level, xp, updated_at)`; XP is monotonic;
  level advances only when XP threshold AND level-gate assessment pass.

**Dependencies.** Reads mission results (§9), job completions (§7), skill-tree
milestones (§3); writes unlocks consumed by world (§5), jobs (§7), housing (§6).

**Rules and failure cases.**
- No level skipping: gates are sequential. XP cannot be bought, traded, or
  transferred (anti-pay-to-win, [PRD] §4).
- Failing a gate assessment: cooldown + targeted practice missions offered;
  no XP penalty, no public failure mark.
- Failure: assessment service down → progression pauses gracefully, already-
  earned XP is never lost; client must never trust local XP math (server-authoritative).

**Data model + API.** `player_levels`, `level_definitions(level, xp_required,
gate_assessment_id, unlocks_json)` (static content, versioned).
`POST /progression/assess`, `GET /progression/me`. [ARCH] §2.

**Security.** XP/level are server-authoritative; assessment attempts are
rate-limited; gate assessments use hidden tests + independent-challenge mode
(AI solution generation disabled).

**Testing.** Level-curve simulation (median hours per level within design
bands); gate integrity tests (cannot advance without passing); rollback tests
on assessment failure. [TEST] §3.

---

## §3 — Skill trees per programming discipline

**Purpose + UX.** While levels track breadth, skill trees track depth:
disciplines (e.g., Web Frontend, Systems Programming, Data Engineering, Game
Development, Audio/Video Engineering, Embedded, AI Engineering, Security) each
have a tree of nodes (concepts → techniques → capstones). Players see exactly
what they have mastered and what unlocks next; nodes light up from verified
evidence, never from watching a video.

**Inputs / outputs / state changes.**
- In: verified mission/test outcomes tagged with concept IDs, capstone reviews.
- Out: node unlocks, mastery badges, cross-tree synergy hints ("your SQL
  indexing node accelerates the Backend tree").
- State: `skill_nodes(player_id, tree_id, node_id, state, evidence_ref)`;
  states: locked → available → in_progress → mastered.

**Dependencies.** Evidence comes from §9 (assessments) and §10 (project
reviews); gates §2 level-ups; feeds job eligibility (§7) and city access (§5).

**Rules and failure cases.**
- A node masters only on passing its evidence bar (usually hidden tests or a
  capstone review); partial credit does not exist — nodes are binary.
- Trees are non-linear: players choose branches; required trunks are minimal.
- Failure: evidence service mis-tags a concept → audit log allows manual
  correction by admins; player-facing state must show *why* a node is locked.

**Data model + API.** `skill_trees(tree_id, version)` + `skill_nodes_def`
static content; `player_skill_nodes` per-player state.
`GET /skills/me`, `GET /skills/trees`. [ARCH] §2.

**Security.** Node state is server-authoritative; evidence references are
content-hashed to prevent replay.

**Testing.** Tree integrity (no orphan nodes, all paths reachable);
evidence-mapping tests (each mission maps to ≥1 node); mastery replay tests.
[TEST] §3.

---

## §4 — Programming languages, frameworks, technology specializations

**Purpose + UX.** Languages are career paths, not skins. Choosing Python opens
the automation/AI/data questline; C opens systems/embedded; JS/TS opens web —
each with its own jobs, mentors (NPC), equipment, and districts. Frameworks
(Django, React, Bevy…) are specializations *within* a language path, unlocked
after core mastery. The embedded real VS Code + language servers make every
path feel professional from day one.

**Inputs / outputs / state changes.**
- In: player path choice (reversible with a cost), mission completions,
  framework unlock conditions.
- Out: path questline, path-specific jobs/equipment/NPCs, specialization badges.
- State: `player_paths(player_id, language, started_at, primary bool)`;
  players may cross-train (multiple paths, one primary).

**Dependencies.** Paths filter missions (§9), jobs (§7), city districts (§5);
language servers/extensions provided by the IDE platform; execution via
[ARCH] §3 (wasm runners + sandbox backend).

**Rules and failure cases.**
- Switching primary path keeps earned levels but starts the new path's
  questline at its entry assessment (no free mastery).
- A language path is "supported" only if it has: a working runner
  (in-browser or sandbox), a language server, and ≥10 missions. Unsupported
  languages appear in the picker as "editor only" — never as a path.
- Failure: runner outage for a path language → missions for that path pause
  with a clear banner; player can cross-train meanwhile.

**Data model + API.** `language_paths(language, tier, runner_kind, ls_status,
mission_count)` static registry; `player_paths` per-player.
`GET /paths`, `POST /paths/select`. [ARCH] §2, §3.

**Security.** Runner registry is server-controlled; clients cannot inject
runners. Sandbox execution rules per [ARCH] §3.

**Testing.** Per-language smoke tests (run hello-world through each runner);
path completeness audit (each path meets the support bar); LS availability
probes. [TEST] §4.

---

## §5 — World map, cities, districts, travel

**Purpose + UX.** A cozy 2D explorable world (see [WORLD]). Cities are the
game's chapters; districts are its neighborhoods. Travel is quick but
meaningful — arriving in a new city is a reward moment. Full map spec in
[WORLD]; this section defines the system rules.

**Inputs / outputs / state changes.**
- In: player position, travel requests, competency proofs.
- Out: rendered map, unlocked fast-travel points, district access changes.
- State: `player_location(player_id, city_id, district_id)`; `unlocked_cities`.

**Dependencies.** Unlock conditions read §2 (level) + §3 (skill nodes) —
competency-gating, never level-number alone ([PRD] concept). Services per
location defined in [WORLD].

**Rules and failure cases.**
- A city unlocks when its *competency requirements* are met (e.g., Systems
  District needs "C memory management" node + L16), not merely on level.
- Travel is free within unlocked cities; inter-city travel has a small
  in-game fee (money sink) except the first arrival (free, celebratory).
- Failure: gating service disagreement → fail closed (stay locked, log,
  alert); never strand a player in an unloaded district (fallback spawn:
  starter room).

**Data model + API.** `cities`, `districts` static content; `player_location`,
`unlocked_cities`. `POST /world/travel`, `GET /world/map`. [ARCH] §5.

**Security.** Location state server-authoritative; travel requests validated
against unlocks (no client-side teleport).

**Testing.** Gating matrix tests (every city × requirement combos); travel
soak tests; fallback-spawn tests. [TEST] §5.

---

## §6 — Housing, equipment, inventory, personal finances

**Purpose + UX.** The material layer: a room that grows from a starter bunk to
a studio; computers/phones/servers/boards that are *stat items* (faster
computer = shorter simulated build times, more concurrent missions);
inventory of components and consumables; a wallet with honest bookkeeping.
Money is earned by work (§7), spent on progression-relevant things.

**Inputs / outputs / state changes.**
- In: purchases, rent payments, equipment upgrades, mission rewards.
- Out: inventory, equipped loadout, balance, rent-due reminders.
- State: `wallets(player_id, balance)`, `inventory(player_id, item_id, qty)`,
  `housing(player_id, property_id, rent_due)`; all mutations are ledgered.

**Dependencies.** Income from §7; equipment stats modify mission parameters
(§9); housing gates Phase-2 company formation (§7).

**Rules and failure cases.**
- Phase 1 ships decoratable homes locally (`housing.json`: workstation, decor
  and trophy slots with derived home-lab benefits). Ownership, rent and visit
  permissions become server records in Phase 2.
- Every money movement is a double-entry ledger row; balances are derived,
  never stored as the source of truth.
- Equipment stats affect *convenience* (speed, slots), never assessment
  outcomes — no pay-to-win.
- Rent: missing rent downgrades housing tier after a grace period; the
  player is never locked out of missions for being broke (starter safety net).
- Failure: ledger inconsistency → freeze affected wallet, alert, reconcile
  from ledger; never "fix" by editing balances.

**Data model + API.** `items`, `properties` static; `wallets`, `ledger`,
`inventory`, `housing`. `POST /shop/buy`, `GET /wallet`, `GET /inventory`.
[ARCH] §5.

**Security.** All purchases server-validated (price, ownership, funds);
idempotency keys on buy calls; no client-side balance.

**Testing.** Ledger invariant tests (sum of movements = balance, always);
property-based tests on buy/refund flows; grace-period tests. [TEST] §6.

---

## §7 — Jobs, contracts, companies, employees, business revenue

**Purpose + UX.** The economy's engine. Jobs are bite-sized paid tasks
(fix a bug, build a page); contracts are larger scoped engagements with
milestones and reviews; companies (Phase 2+) let players hire NPCs/employees,
take bigger contracts, and earn recurring revenue. This is where "learn to
code" becomes "get paid to code" — inside the fiction.

**Inputs / outputs / state changes.**
- In: job applications, contract bids, milestone submissions, hire/fire,
  salary payments.
- Out: payouts, contract status, company ledger, employee rosters.
- State: `jobs`, `contracts`, `companies`, `employees`, `company_ledger`.

**Dependencies.** Job eligibility reads §2/§3/§4 (level, skills, path);
mission-quality history feeds reputation (§1); payouts write to §6 wallets.
Phase 1 ships a **local company preview** — dashboard, roster, focus, rank and a
convenience payout preview, with no escrow. Authoritative contracts, escrow and
the L25 + property + moderation gate are Phase 2.

**Rules and failure cases.**
- Job matching is skill-based: a player only sees jobs they are plausibly
  qualified for; applying above level is blocked with guidance, not shame.
- Contracts have milestone escrow: payment releases per verified milestone;
  disputes go to a review queue (Phase 2; Phase 1 contracts don't exist).
- Companies (Phase 2): formation requires L25 + a property + clean moderation
  record; employee salaries are recurring ledger movements; bankruptcy
  dissolves the company but never touches personal progression.
- Failure: payout double-spend → idempotency keys + ledger reconciliation;
  abandoned contract → escrow returns to issuer after timeout.

**Data model + API.** `job_defs` static; `job_instances`, `contracts`,
`contract_milestones`, `companies`, `employees`, `company_ledger`.
`POST /jobs/{id}/apply`, `POST /jobs/{id}/submit`, `GET /jobs/feed`.
[ARCH] §5, §7.

**Security.** Payouts server-side only; application/submission rate limits;
contract terms are immutable once accepted (hash-pinned).

**Testing.** Matching-rule tests; escrow lifecycle tests; idempotency tests;
bankruptcy drill. [TEST] §7.

---

## §8 — Multiplayer, friends, guilds, teams, social spaces

**Purpose + UX.** (Designed now; Phase 1 ships a **local preview** and Phase 2+
adds real players.) Learning is social: friends list, guilds (study groups with
shared goals), teams for collaborative missions, and social spaces (plazas,
labs) where players show off projects. Presence is lightweight — this is a cozy
world, not a lobby shooter. The Phase 1 preview is six written NPC contacts,
deterministic keyword replies, unread counts and 3-person teams — openable from
the pause menu; no real players and no free chat.

**Inputs / outputs / state changes.**
- In: friend requests, guild create/join, team invites, chat messages,
  collaborative mission actions.
- Out: presence, chat, shared mission state, guild leaderboards (effort-based).
- State: `friendships`, `guilds`, `guild_members`, `teams`, chat history (TTL).

**Dependencies.** Collaborative missions reuse §9 assessment (per-player
grading — no free-riding: each member must pass their own hidden tests);
reputation (§1) visible in teams; moderation (§13) governs all chat.

**Rules and failure cases.**
- No global open chat in Phase 2 — friends/guild/team channels only
  (safety by design).
- Collaborative missions grade individually; team success requires every
  member's pass (prevents boosting).
- Failure: realtime outage → missions degrade to solo mode with a notice;
  chat history is ephemeral (TTL) and never the system of record for grades.

**Data model + API.** `friendships`, `guilds`, `guild_members`, `teams`,
`team_members`; realtime via [ARCH] §4 (WebSocket/SSE channels).
`POST /friends/request`, `POST /guilds`, `POST /teams`. [ARCH] §4.

**Security.** All messages filtered (§13); block/mute is one tap and
immediate; team invites require mutual consent; no DMs from strangers.

**Testing.** Consent-flow tests; free-rider tests (team pass requires all
individual passes); realtime failover tests. [TEST] §8.

---

## §9 — Learning engine, assessments, hints, independent challenges

**Purpose + UX.** The pedagogical core. Missions teach a concept (short
briefing + interactive example), then demand real code: write → run → debug →
test. Public tests show what's expected; hidden tests prove understanding.
Hints are Socratic (nudge, not answer) and cost nothing but are logged;
independent challenges lock AI assistance entirely.

**Inputs / outputs / state changes.**
- In: mission starts, code submissions, hint requests, assessment attempts.
- Out: run output, test results (public detail / hidden pass-fail), hint
  text, XP, skill-node evidence, assessment verdicts.
- State: `mission_attempts`, `assessment_results`, `hint_usage`.

**Dependencies.** Execution via [ARCH] §3 (`/execute` for runs, `/assess`
for grading — the quest verification mechanic); evidence feeds §2/§3;
AI modes from §12; difficulty calibrated by [TEST] §9 analytics.

**Rules and failure cases.**
- Grading is deterministic and reproducible: same code + same tests =
  same verdict, always (sandbox images are pinned).
- Hints never contain the solution; three escalating hint tiers, then a
  "worked example of a *different* problem" (teaches transfer, not copying).
- Independent challenges: AI assistant disabled, no hint tiers, hidden tests
  only; required at every level gate and every skill-tree capstone.
- Failure: grading disagreement (flaky test) → quarantine the mission,
  auto-refund attempts, alert content team; never fail a player on an
  infrastructure error.

**Data model + API.** `missions` (static, versioned, content-hashed),
`mission_attempts`, `assessment_results`, `hints`.
`POST /missions/{id}/submit` → runs `/assess` internally;
`POST /missions/{id}/hint`. [ARCH] §2, §3.

**Security.** Submissions run only in sandboxes; hidden test content never
reaches the client (only pass/fail + safe diagnostics); attempt rate limits;
plagiarism signals logged (see §13).

**Testing.** Mission quality bar: each mission playtested by ≥3 humans;
hidden-test discrimination tests (a memorized public-test solution must
FAIL hidden tests); determinism tests (100 runs, identical verdicts).
[TEST] §9.

---

## §10 — Project creation, code hosting, publishing, sales

**Purpose + UX.** Players build real things: multi-file projects in the
in-game IDE, hosted in personal repos, publishable to the in-game marketplace
(portfolio pieces, templates, plugins, games). Selling is for in-game
currency (Phase 1–2); the portfolio doubles as the "Proof of Skill" showcase.

*Phase 1 build:* multi-file projects, workspaces, a test runner, a JS debugger,
a terminal and a git panel ship in the IDE. Sandbox-runtime projects also get
**revision history** through the execution worker's submissions store
(`saveRevision` / `compareRevisions`): each run is content-hashed and diffable.
Hosted repos, publishing and the marketplace are Phase 2.

**Inputs / outputs / state changes.**
- In: project files, publish requests, price (in-game currency), purchases.
- Out: hosted repo, published listing, sales ledger entries, portfolio page.
- State: `projects`, `project_files` (content-addressed), `listings`, `sales`.

**Dependencies.** IDE file APIs (§4 platform); execution for "try it" demos
([ARCH] §3, demo runs are sandboxed and read-only); sales write to §6 ledger.

**Rules and failure cases.**
- Published projects are scanned before listing (malware/secret scan;
  executable artifacts are never directly downloadable — source only).
- Prices are in-game currency only (Phase 1–2); no real-money sales
  ([PRD] OPEN-4).
- Takedown: infringing or malicious listings are delisted immediately with
  notice; sales are refunded from the seller's wallet (or frozen if empty).
- Failure: storage outage → projects are local-first (localStorage/OPFS
  cache), sync resumes; never lose unsaved work silently (dirty-state UI).

**Data model + API.** `projects`, `project_files(hash, bytes)`,
`listings`, `sales`. `POST /projects`, `POST /projects/{id}/publish`,
`POST /listings/{id}/buy`. [ARCH] §6.

**Security.** Content-addressed storage; upload size caps; secret scanning
(API keys in published code are rejected with guidance); purchase idempotency.

**Testing.** Scan-pipeline tests (known-bad samples blocked); publish/buy
lifecycle tests; offline-edit conflict tests. [TEST] §10.

---

## §11 — (platform) Embedded IDE — the in-game computer

**Purpose + UX.** The player's computer *is* genuine VS Code in the browser
(real workbench via monaco-vscode-api, real extensions, real language
servers). It is the surface for all coding: missions, jobs, projects,
debugging. Devs choose languages by installing real extensions — the game
never hand-builds language tooling.

**Inputs / outputs / state changes.**
- In: file operations, extension installs, run/debug/test commands, backend
  config (sandbox URL + key).
- Out: editor UI, diagnostics, run output, test reports, artifacts.
- State: files (via FileSystemProvider → localStorage), settings, backend
  config — same keys as the classic IDE for continuity.

**Dependencies.** Game shell drives it via `window.GameIDE`
(setLanguage/getCode/setCode/runCode/onRun/runTests/setBackend/...);
execution via wasm runners + [ARCH] §3; missions (§9) inject starter code
and read results.

**Rules and failure cases.**
- The GameIDE API contract is versioned; the game shell feature-detects.
- Extension installs are limited to the curated Open VSX allowlist (no
  arbitrary vsix URLs — supply-chain safety).
- Failure: workbench fails to load → fallback to the classic IDE
  (`game-ide/classic/`) with a notice; player files are shared, so nothing
  is lost.

**Data model + API.** No new server tables (files are client-local in
Phase 1); `window.GameIDE` JS API (documented in `docs/architecture.md`).
[ARCH] §3 (runners), §2 (missions bridge).

**Security.** Extension allowlist; language servers run in workers with no
network; code execution only via the sanctioned runner/backend paths.

**Testing.** GameIDE API conformance tests; per-language-server load tests;
fallback-to-classic drill. [TEST] §4.

---

## §12 — AI assistance and independent skill verification

**Purpose + UX.** An in-game AI mentor with five modes — **learning**
(explains, asks guiding questions), **debugging** (interprets errors without
writing the fix), **code review** (critiques, suggests improvements),
**independent challenge** (no AI at all), **professional** (AI allowed, but
the player must review, test, explain, and take responsibility). The AI
accelerates learning; it never substitutes for it.

**Inputs / outputs / state changes.**
- In: player questions, code context, error output, mode selection.
- Out: explanations, guiding questions, review comments (never milestone
  solutions).
- State: `ai_sessions`, per-mode usage counters (rate limits differ by mode).

**Dependencies.** Reads current file/mission context (§9, §11); milestone
gates (§2) force independent-challenge mode; misuse signals feed §13.

**Rules and failure cases.**
- Hard rule: in learning/debugging/review modes the AI must not emit a
  complete working solution to the current mission; independent-challenge
  mode disables the AI entirely (UI hidden, API refused).
- Professional mode requires the player to pass an "explain your change"
  check on milestones (short written or oral-style Q&A graded by rubric).
- AI output is labeled as AI; it is never presented as verified or correct.
- Failure: filter bypass attempt → session flagged, mode locked to
  independent for a cooldown; repeated abuse → moderation (§13).

**Data model + API.** `ai_sessions`, `ai_events` (prompt/response hashes,
mode, mission ref — full text retained only per retention policy).
`POST /ai/ask` (mode-pinned, context-scoped). [ARCH] §8.

**Security.** Prompts are scoped to the current mission/file (no free-form
exfiltration channel); PII redaction before logging; per-player rate limits;
model output scanned for solution-shaped content in locked modes.

**Testing.** Red-team prompt suite (solution-extraction attempts must fail);
mode-enforcement tests; "explain your change" rubric calibration. [TEST] §11.

---

## §13 — Moderation, cybersecurity, cheating prevention, player safety

**Purpose + UX.** Trust infrastructure. Players must believe that levels mean
something, the economy is fair, and the community is safe. Mostly invisible
when working; decisive and transparent when it acts.

**Inputs / outputs / state changes.**
- In: reports, automated signals (chat filter hits, anomaly scores, plagiarism
  signals), assessment forensics.
- Out: warnings, mutes, suspensions, bans, content takedowns, appeal outcomes.
- State: `reports`, `moderation_actions`, `appeals`, `trust_scores` (internal).

**Dependencies.** Receives signals from chat (§8), assessments (§9),
economy (§6/§7), AI misuse (§12); can restrict §1 profiles and §8 social.

**Rules and failure cases.**
- Cheating taxonomy: (a) solution replay/paste rings → hidden-test forensics
  + timing analysis; (b) account sharing for gates → device/behavior signals;
  (c) economy exploits → ledger anomaly detection; (d) AI misuse at
  milestones → mode-lock + review.
- Penalties are graduated and appealable; cheating at a gate invalidates
  that gate only (not the whole account) on first offense.
- Player safety: no open DMs from strangers (Phase 2), one-tap block/mute,
  chat filtered + human-reviewed, under-16 protections per [PRD] OPEN-3.
- Cybersecurity: all player content is untrusted by default (sandboxed,
  scanned); game servers never execute player code; published projects
  scanned (§10).
- Failure: false positive → appeal queue with SLA; moderation actions are
  logged immutably and reversible.

**Data model + API.** `reports`, `moderation_actions`, `appeals`,
`forensics_events`. `POST /reports`, internal `POST /moderation/action`
(admin-only). [ARCH] §9, §10.

**Security.** Moderation tools are role-gated with audit trails; forensics
data access is minimized and retention-bounded; no automated permanent bans
without human review.

**Testing.** Filter precision/recall suites; forensics detection tests on
synthetic cheating; appeal SLA drills; penetration tests per release.
[TEST] §12.

---

## Ownership map (completion-criterion checklist)

| System | Owner (role) | Security boundary | Acceptance criteria |
|---|---|---|---|
| §1 Identity | Platform | Auth + profile PII | Signup→profile→rename flows pass; no PII in public API |
| §2 Progression | Game design | Server-authoritative XP | Cannot advance without gate pass; XP math audited |
| §3 Skill trees | Game design | Evidence integrity | Every node has an evidence bar; no orphans |
| §4 Languages | Platform | Runner registry | Each path meets the support bar (§4 rules) |
| §5 World | World design | Server-authoritative location | Gating matrix green; no strand bugs |
| §6 Housing/economy | Economy design | Ledger invariants | Double-entry always balances; idempotent buys |
| §7 Jobs/companies | Economy design | Payout authority | Escrow lifecycle correct; no double-spend |
| §8 Multiplayer | Social design | Consent + chat safety | No stranger DMs; team grading is individual |
| §9 Learning | Content design | Hidden-test secrecy | Hidden tests discriminate; verdicts deterministic |
| §10 Projects | Platform | Content scanning | Malicious samples blocked; offline-safe |
| §11 IDE | Platform | Extension allowlist | GameIDE API conformance; LS verified per language |
| §12 AI | AI safety | Mode enforcement | Red-team suite green; milestones AI-free |
| §13 Moderation | Trust & safety | Human review for bans | Appeals SLA met; FP rate tracked |

**OPEN items carried:** [PRD] OPEN-1..OPEN-4. No additional system-level
OPENs — every system above is defined to the Phase 1 buildable level, with
Phase 2+ items explicitly marked.
