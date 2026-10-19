# DEVWORLD — Testing Strategy

**Version:** 0.1 (2026-10-19)
**Principle:** every claim the game makes about a player must be *provable* —
by tests, not by vibes. This document ties directly to the "how it is tested
and measured" section of each [GDD] system (§1–§13).

---

## 1. Test pyramid for DEVWORLD

| Layer | What | Where | Gate |
|---|---|---|---|
| Unit | Math with invariants (XP, ledger, reputation, gating) | CI, every PR | Must pass |
| Contract | API shapes, event schemas, GameIDE API | CI | Must pass |
| Integration | Service→service (mission submit → /assess → verdict) | CI + staging | Must pass |
| Sandbox | Runner determinism, isolation, hidden-test secrecy | CI (mock) + Docker host (real) | Must pass |
| Content | Every mission playtested by ≥3 humans; hidden-test discrimination | Content pipeline | Mission blocked until green |
| Red-team | Cheating, AI bypass, security | Pre-release + continuous | Findings triaged in 48h |
| Playtest | Retention, difficulty curve, fun | Cohorts | Tunes numbers, never gates |

## 2. Per-system test maps

- **[GDD] §1 Identity:** handle validation fuzz (unicode, length, reserved);
  reputation earn/decay boundary tests; public-profile PII scan (no email/IP/phone in responses).
- **[GDD] §2 Progression:** level-curve simulation (median hours/level within
  design bands, [PROG] tuning notes); gate integrity (no advance without pass);
  server-authority tests (client XP spoof rejected).
- **[GDD] §3 Skill trees:** static analysis of tree content (no orphans, all
  nodes reachable); evidence mapping (each mission → ≥1 node); mastery replay
  (recompute from evidence log = current state).
- **[GDD] §4 Languages:** per-language hello-world through every runner on
  every release; path support-bar audit (runner + LS + ≥10 missions);
  LS availability probes with graceful degradation.
- **[GDD] §5 World:** gating matrix (cities × requirement combos, incl.
  fail-closed); travel idempotency (double-travel charges once); fallback-spawn drill.
- **[GDD] §6 Economy:** ledger invariants as CI assertions
  (Σ movements ≡ balance, per wallet, per company); property-based buy/refund
  tests; rent grace-period tests; anomaly-detector precision/recall on
  synthetic exploits.
- **[GDD] §7 Jobs/companies:** matching-rule tests; escrow lifecycle
  (fund → milestone → release/dispute/timeout); payout idempotency;
  bankruptcy drill (company dissolves, progression untouched).
- **[GDD] §8 Multiplayer:** consent flows (no DM without mutual consent);
  free-rider tests (team mission requires every member's individual pass);
  realtime failover (drop to solo mode, no grade loss).
- **[GDD] §9 Learning:** mission quality bar (3 human playtests each);
  hidden-test discrimination (a solution that only memorizes public tests
  MUST fail hidden tests — automated); determinism (100 identical runs →
  identical verdicts); flaky-mission quarantine drill.
- **[GDD] §10 Projects:** scan pipeline vs. known-bad corpus (malware,
  secrets); publish→buy lifecycle; offline-edit conflict resolution.
- **[GDD] §11 IDE:** GameIDE API conformance suite (every method, incl.
  version negotiation); per-language-server load tests (real diagnostics
  appear); fallback-to-classic drill.
- **[GDD] §12 AI:** red-team prompt suite (solution-extraction in locked
  modes must fail); mode-enforcement tests (independent-challenge refuses);
  "explain your change" rubric calibration (inter-rater agreement ≥0.8).
- **[GDD] §13 Moderation:** filter precision/recall on labeled corpora;
  forensics detection on synthetic cheating (paste rings, timing anomalies);
  appeal SLA drills; pen-test per release.

## 3. The integrity metrics (always-on dashboards)

These are the game's vital signs ([PRD] §5):

1. **Hidden/public gap:** per mission, `pass_rate(public) − pass_rate(hidden)`.
   Alert if >15pp sustained — the signature of memorization or leaks.
2. **Gate legitimacy:** % of gate passes later contradicted by job performance
   (should stay low; high = gates too easy).
3. **Verdict determinism:** % of re-graded submissions with identical verdicts
   (target: 100%).
4. **AI bypass rate:** % of locked-mode sessions with solution-shaped output
   (target: 0; alert on any).
5. **Economy health:** ledger reconciliation (continuous), payout anomaly rate.
6. **Progression pace:** median hours per level vs. design bands ([PROG]) —
   retune content, never the gates.

## 4. Content pipeline gates

No mission ships without: (a) content hash + version; (b) 3 human playtests;
(c) hidden-test discrimination proof; (d) determinism run (n=100);
(e) difficulty calibration (median solve time within band); (f) hint-tier
review (hints nudge, never solve). Same bar for assessments, scaled by stakes
(gate assessments get 5 playtesters + red-team review).

## 5. Release gates

- **Phase 1 alpha:** all unit/contract/integration green; 10 missions through
  the content pipeline; sandbox isolation verified on a real Docker host;
  perf budget met (time-to-first-mission <60s).
- **Public beta:** + red-team clean (no criticals), [PRD] OPEN-3 closed,
  appeal SLA demonstrated in drills, integrity dashboards live.
- **Phase 2/3:** each new system ships behind its per-system test map
  (§2) — no system launches untested.

## 6. What we do NOT test (explicitly)

- We do not A/B test gate difficulty against integrity (gates are not
  growth levers).
- We do not test "engagement hacks" that conflict with [PRD] pillars
  (no dark patterns, no pay-to-win — these are design invariants, not
  experiments).

## OPEN

- **OPEN-T1 — Load-test targets.** Concurrent players, submissions/sec for
  Phase 1: closes after hosting decision ([ARCH] OPEN-A1).
- **OPEN-T2 — Accessibility audit scope.** WCAG target level and audit
  vendor: closes before public beta.
