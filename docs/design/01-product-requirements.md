# DEVWORLD — Product Requirements Document

**Version:** 0.1 (design-lock draft, 2026-10-19)
**Status:** Pre-production. Defines rules before systems are built.
**Companion docs:** `02-game-design-document.md` (systems), `03-world-specification.md` (map),
`04-progression-50-levels.md` (levels), `05-system-architecture.md` (services),
`06-testing-strategy.md` (verification).

---

## 1. Product definition

DEVWORLD is a **web-based open-world RPG that gamifies learning to code**.
The player arrives in a small town with a basic computer, learns real programming
by doing real jobs and missions, and progresses from Beginner to Creator across
50 levels — choosing programming languages as career paths, earning in-game money,
founding companies, and proving skill through verified, test-graded achievements
("Proof of Skill").

The defining principle: **knowledge gained in the game must work outside the game.**
Players write real code in real languages, debug real failures, and pass hidden
tests they have never seen. Selecting the right multiple-choice answer is never
the mechanic; building working software is.

## 2. Audience

- **Primary:** Aspiring developers aged ~14–30 learning their first or second
  programming language; self-taught learners who bounce off passive tutorials.
- **Secondary:** Junior developers seeking structured, verifiable skill proof for
  portfolios; hobbyists exploring new disciplines (embedded, AI, security).
- **Non-audience:** Players seeking pure entertainment without learning effort;
  experienced engineers wanting advanced reference material (served only at
  levels 36+).

## 3. Product pillars

1. **Real engineering, not puzzles.** Real languages, real runtimes, real tests,
   real debugging workflows. The in-game computer is genuine VS Code
   (embedded via monaco-vscode-api) with real language servers and extensions.
2. **Proof over promises.** Progression unlocks on demonstrated competency —
   hidden tests, independent challenges, code review — never on time played or
   content consumed.
3. **A living developer world.** Persistent economy, companies, contracts,
   reputation, and social play. Skills unlock opportunities; products generate
   revenue; the world reacts to what the player can build.
4. **Independence from AI.** An in-game AI assistant teaches and reviews, but
   milestones must be earned without generated solutions. The game trains
   players to verify, not to paste.
5. **Playable in a browser, instantly.** No installs. C++ game shell compiled
   to WebAssembly; IDE and execution run in the page or against the sandbox
   backend.

## 4. Scope

### Phase 1 — Playable foundation (build now)

- One starter town (Starter Town), one player profile.
- The embedded IDE as the in-game computer terminal (GameIDE API).
- Safe code execution: in-browser WASM runners + sandbox backend
  (`POST /execute`, `POST /assess`).
- Python fundamentals + beginner SQL learning paths.
- ~10 carefully designed missions with public + hidden tests.
- XP, in-game money, inventory, unlockable equipment.
- A small portfolio of player projects.
- **Success criterion:** a beginner learns useful programming skills, builds a
  working project, and can demonstrate understanding of it.

### Phase 2 — The multiplayer developer world (later)

Friends, chat, teams, collaborative missions; player-owned projects and company
management; more languages, career paths, cities; equipment, housing, in-game
economy; project reviews, reputation, skill-based job boards; hackathons.

### Phase 3 — The technology metaverse (north star)

All 50 levels; advanced databases, compilers, OS, networking; embedded, game
engines, media software; AI engineering and security labs; advanced company
operations and creator marketplace; real industry partnerships and verified
opportunities where feasible.

### Explicitly out of scope (all phases)

- Real-money payouts or crypto integration (see OPEN-4).
- Gambling, loot boxes, or pay-to-win mechanics.
- User-generated executable content shared without sandbox review.

## 5. Success criteria

| Level | Criterion | Measure |
|---|---|---|
| Product | Beginners reach demonstrable skill | % of active players passing an independent assessment at their level (target: >40% by Phase 1 end) |
| Learning | Knowledge transfers outside the game | Cohort survey: "built something real outside DEVWORLD" (target: >25% of L10+ players) |
| Retention | Players return for the world, not just lessons | D7 retention of mission completers (target: >30%) |
| Integrity | Skill claims are trustworthy | Hidden-test pass rate vs. public-test pass rate gap stays <15pp (cheating signal if wider) |
| Performance | Instant play | Time-to-first-mission <60s on a mid-range phone; IDE interactive <5s on broadband |

## 6. Constraints and assumptions

- **Web only.** No native clients in Phase 1. Target: modern Chrome/Edge/Firefox/Safari.
- **Execution safety is non-negotiable.** All player code runs in WASM sandboxes
  or one-shot Docker containers (no network, capped CPU/memory/time). Player
  code never runs on game servers. See `05-system-architecture.md` §3 and the
  existing `execution-worker/docs/threat-model.md`.
- **AI is assistive.** The in-game AI never writes milestone solutions; see
  `02-game-design-document.md` §12.
- **Content honesty.** The game never presents simulated outcomes as real-world
  credentials. "Proof of Skill" achievements describe exactly what was tested.
- **Accessibility baseline:** keyboard-playable, colorblind-safe palettes,
  text scaling, reduced-motion mode (defined per system in the GDD).

## 7. OPEN items

- **OPEN-1 — Monetization model.** Subscription vs. cosmetic-only vs. grant-funded:
  decision needed before Phase 2 economy tuning. Closes when the founder picks
  a model; until then, no real-money hooks in any design.
- **OPEN-2 — Minimum device spec.** Target phone/browser matrix undecided;
  closes after Phase 1 performance profiling.
- **OPEN-3 — Age rating / child safety posture.** COPPA/GDPR-K implications of
  accounts for under-16 players; closes with legal review before public beta.
- **OPEN-4 — Real-money marketplace.** Phase 3 "genuine paid work" needs
  contracts, payments, moderation, fraud protection — a separate product track.
  Explicitly not designed here.
