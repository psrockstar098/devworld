# DEVWORLD — 50-Level Progression Specification

**Version:** 0.1 (2026-10-19)
**Reading guide:** Levels are *broad progression* (world, jobs, equipment);
skill trees ([GDD] §3) track *per-technology mastery*. Every level lists the
same eight fields. XP thresholds are illustrative curves (final tuning in
[TEST] §3 playtests); **gate assessments are never illustrative** — each is a
real, specified hurdle.
**Format per level:** Entry · Competencies (+optional) · Locations/services ·
Jobs/contracts · Equipment/property · New tech · Rewards/progression ·
Assessment.

**Phase 1 build status:** the slice that ships is Levels 1–5 plus the early
Tier-2 arrival content — 44 missions (`m01`–`m44`, including the three hidden
dungeon challenges in `dungeons.json` and six Tier-2 skill missions for the
control-flow, debugging, tooling and performance topics, plus three defensive-security missions for the security area, three networking missions for the networking area, and three machine-learning missions for the AI area, and three game missions for the game area, and three embedded missions for the embedded area, and three cloud missions for the cloud area, and three compiler missions for the compilers area), all four cities,
regional economies, decoratable homes and a local contacts/company/portfolio
layer. Levels 6–50
describe the target curve; XP values are illustrative until playtest tuning.

The **progression engine** (design `09-progression-engine.md`) now sits
underneath this ladder: levels stay breadth, while skill mastery, language
proficiency, career specialization, project experience, reputation/reliability,
achievements and company leadership are separate, derived records. Every level
gate, city route, company eligibility rule and technical privilege is one
data-driven entry in `content/unlocks.json`, evaluated against the player's
demonstrated capability (never the level alone) and shown with the reason it was
earned or not in the **Progression & unlocks** dashboard.

---

## TIER 1 — THE BEGINNER (Levels 1–5)
*Theme: first contact with real code. Python + SQL. Starter Town.*

### Level 1 — First Boot
- **Entry:** New account; tutorial.
- **Competencies:** Run a program; read output; use the IDE terminal. | *Optional:* change the welcome program's text.
- **Locations/services:** Home Row (room), Computer Lab (free terminal).
- **Jobs/contracts:** None yet (errands: "deliver this USB stick" for 5 coins — movement tutorial).
- **Equipment/property:** Starter bunk (free), lab computer (shared).
- **New tech:** Python `print`, variables.
- **Rewards/progression:** 100 XP → L2 on first successful run.
- **Assessment:** None (participation gate).

### Level 2 — Variables & Types
- **Entry:** L1 complete.
- **Competencies:** Variables, strings, numbers, input/output. | *Optional:* string-method playground.
- **Locations/services:** Computer Lab; Data Kiosk opens (browse-only).
- **Jobs/contracts:** 1-coin micro-errands.
- **Equipment/property:** Personal (slow) laptop awarded.
- **New tech:** `input()`, f-strings, type basics.
- **Rewards/progression:** 250 XP; laptop unlock.
- **Assessment:** 3-question interactive check (unlimited retries, hints allowed).

### Level 3 — Logic
- **Entry:** 250 XP.
- **Competencies:** `if/else`, comparisons, boolean logic. | *Optional:* logic-puzzle side quests.
- **Locations/services:** Mission Board Plaza opens (L1–3 missions).
- **Jobs/contracts:** "Fix the shop sign" (condition bug) — first paid bug fix, 20 coins.
- **Equipment/property:** Laptop upgrade I (faster simulated runs).
- **New tech:** Conditionals, `and/or/not`.
- **Rewards/progression:** 500 XP; title "Tinkerer".
- **Assessment:** Debug a 10-line program with 2 planted bugs (public tests).

### Level 4 — Loops
- **Entry:** 500 XP.
- **Competencies:** `for`/`while`, iteration, accumulators. | *Optional:* pattern-printing challenges.
- **Locations/services:** Fix-It Shop opens (NPC Mara).
- **Jobs/contracts:** Shop inventory counter (loop job), 35 coins.
- **Equipment/property:** Backpack (inventory slots +4).
- **New tech:** Loops, `range`, `break/continue`.
- **Rewards/progression:** 900 XP.
- **Assessment:** Write a loop solution passing public + 2 hidden tests.

### Level 5 — Functions & First Proof
- **Entry:** 900 XP.
- **Competencies:** Functions, parameters, return values, basic scope. | *Optional:* refactor a mission solution into functions.
- **Locations/services:** Full Starter Town access; Bus Stop visible (locked).
- **Jobs/contracts:** "Automate Mara's receipts" (multi-function job), 80 coins.
- **Equipment/property:** Laptop upgrade II; room decoration unlocks.
- **New tech:** Functions, scope, intro to testing mindset.
- **Rewards/progression:** 1,400 XP; title "Beginner"; **bus ticket earned** (usable at the Bus Stop to reach Startup City).
- **Assessment:** **GATE — Independent:** write 3 functions from a spec, no AI, no hints, hidden tests. Fail → targeted practice, retry in 1h.

---

## TIER 2 — THE BUILDER (Levels 6–10)
*Theme: real software habits. Startup City opens. Git, testing, debugging.*

### Level 6 — Arrival
- **Entry:** L5 gate passed; 1,400 XP.
- **Competencies:** Navigate Startup City; use the job board. | *Optional:* tour every district.
- **Locations/services:** Startup City (Founders' Row); job board (junior).
- **Jobs/contracts:** Junior bug-fix board (30–60 coins each).
- **Equipment/property:** Apartment rental unlocked (50 coins/week).
- **New tech:** Reading stack traces.
- **Rewards/progression:** 2,000 XP; city arrival cinematic.
- **Assessment:** None (arrival).

### Level 7 — Git & the Command Line
- **Entry:** 2,000 XP.
- **Competencies:** `git init/add/commit/log`, branching basics, CLI navigation. | *Optional:* break and restore a repo.
- **Locations/services:** Team formation office (observe-only).
- **Jobs/contracts:** "Version-control rescue" jobs (fix a repo's history).
- **Equipment/property:** Terminal upgrade (history, autocomplete).
- **New tech:** Git, shell basics.
- **Rewards/progression:** 2,800 XP; "can commit" badge on profile.
- **Assessment:** Interactive: recover a repo from a described mess (sandboxed).

### Level 8 — Data Structures
- **Entry:** 2,800 XP.
- **Competencies:** Lists, dicts, sets, tuples — choosing the right one. | *Optional:* performance intuition quests.
- **Locations/services:** Marketplace Mile (browse).
- **Jobs/contracts:** Data-cleanup contracts (dict-heavy), 100+ coins.
- **Equipment/property:** Laptop upgrade III.
- **New tech:** Core data structures; big-O intuition (no proofs).
- **Rewards/progression:** 3,800 XP.
- **Assessment:** Hidden-test set on structure choice (a list-solution must fail where a dict is required).

### Level 9 — Testing & Debugging
- **Entry:** 3,800 XP.
- **Competencies:** Write tests first, read failures, bisect bugs, use the debugger. | *Optional:* "bug injection" duels vs. NPC.
- **Locations/services:** Hackathon Hall (spectate).
- **Jobs/contracts:** QA bounties (write tests for NPC code), 120 coins.
- **Equipment/property:** Debugger tool unlock (in-IDE).
- **New tech:** `assert`, test runners, breakpoints.
- **Rewards/progression:** 5,000 XP; title "Builder".
- **Assessment:** Given untested code: write tests achieving hidden coverage bar, then fix the planted bug.

### Level 10 — First Full Project
- **Entry:** 5,000 XP.
- **Competencies:** Plan, build, test, and ship a small multi-file project. | *Optional:* publish to the marketplace (no sales yet).
- **Locations/services:** Full Startup City (except Hackathon Hall entry).
- **Jobs/contracts:** "Build the bakery website" (first project-scale job), 300 coins.
- **Equipment/property:** Project hosting slot ×1; apartment discount.
- **New tech:** Project structure, READMEs, requirements files.
- **Rewards/progression:** 6,500 XP; portfolio page created (Proof of Skill #1).
- **Assessment:** **GATE — Independent + review:** ship the project; hidden acceptance tests; 5-minute "explain your design" Q&A (rubric-graded).

---

## TIER 3 — THE ENGINEER (Levels 11–15)
*Theme: professional backend. SQL deep, HTTP/APIs, OS basics.*

### Level 11 — SQL Foundations
- **Entry:** L10 gate passed; 6,500 XP.
- **Competencies:** SELECT, WHERE, JOIN, GROUP BY on real datasets. | *Optional:* dataset bounties at the Data Kiosk.
- **Locations/services:** Data Kiosk (full); Hackathon Hall entry unlocked.
- **Jobs/contracts:** Query bounties (80–150 coins).
- **Equipment/property:** Database GUI tool.
- **New tech:** Relational thinking, SQL.
- **Rewards/progression:** 8,000 XP.
- **Assessment:** Hidden-data query set (queries must be correct, not just output-matching — inspected).

### Level 12 — Schema Design
- **Entry:** 8,000 XP.
- **Competencies:** Normalization, keys, indexes, migrations. | *Optional:* "normalize this mess" timed challenge.
- **Locations/services:** Startup City (all).
- **Jobs/contracts:** "Design the clinic's database" (schema contract), 400 coins.
- **Equipment/property:** Apartment upgrade (home office → +1 concurrent mission).
- **New tech:** DDL, indexing, transactions intro.
- **Rewards/progression:** 10,000 XP.
- **Assessment:** Design review: schema judged on hidden workload tests (a bad index choice fails under load simulation).

### Level 13 — HTTP & APIs
- **Entry:** 10,000 XP.
- **Competencies:** Requests, JSON, REST conventions, status codes. | *Optional:* API-mocking playground.
- **Locations/services:** Relay outpost (Startup City edge).
- **Jobs/contracts:** "Wire the shop to the payment API" (integration job), 450 coins.
- **Equipment/property:** API testing tool.
- **New tech:** HTTP, JSON, auth headers.
- **Rewards/progression:** 12,500 XP; title "Engineer".
- **Assessment:** Integrate with a flaky mock API (retries, error handling) — hidden fault injection.

### Level 14 — Authentication & Security Basics
- **Entry:** 12,500 XP.
- **Competencies:** Hashing vs. encryption, sessions, tokens, OWASP top-3 intuition. | *Optional:* "spot the vuln" gallery.
- **Locations/services:** Same city; security notice board opens.
- **Jobs/contracts:** Security-review micro-jobs (150 coins).
- **Equipment/property:** Security scanner (in-IDE lint).
- **New tech:** Auth flows, password hashing, env secrets.
- **Rewards/progression:** 15,000 XP.
- **Assessment:** Harden a login endpoint; automated attack script must fail (red-team graded).

### Level 15 — Shipping a Complete App
- **Entry:** 15,000 XP.
- **Competencies:** Full-stack assembly: DB + API + frontend + deploy checklist. | *Optional:* custom domain (cosmetic).
- **Locations/services:** All Startup City services.
- **Jobs/contracts:** "Ship the clinic portal" (end-to-end contract), 1,200 coins escrow.
- **Equipment/property:** Deploy pipeline slot; company-formation preview (locked).
- **New tech:** Deployment basics, env config, logs.
- **Rewards/progression:** 18,000 XP; portfolio milestone (Proof of Skill #2).
- **Assessment:** **GATE — Independent:** build and deploy a working app in 48h (game time); hidden acceptance + load tests; no AI; explain architecture.

---

## TIER 4 — THE SPECIALIST (Levels 16–20)
*Theme: choose a discipline. Systems District opens. Frameworks.*

### Level 16 — The Fork
- **Entry:** L15 gate passed; 18,000 XP.
- **Competencies:** Pick a primary path (Python/JS/C/Rust/SQL…); entry assessment for the path. | *Optional:* sample two paths before committing.
- **Locations/services:** **Systems District gate** (competency-checked); path guild halls.
- **Jobs/contracts:** Path-filtered job board.
- **Equipment/property:** Path starter kit (language-specific tools).
- **New tech:** The chosen language's idioms (path questline begins).
- **Rewards/progression:** 21,000 XP; path title (e.g., "Pythonista").
- **Assessment:** Path entry exam (hidden tests in the chosen language).

### Level 17 — Frameworks
- **Entry:** 21,000 XP + path chosen.
- **Competencies:** One major framework in the path (Django/React/Bevy…); conventions over configuration. | *Optional:* framework-vs-vanilla debate quests.
- **Locations/services:** Framework dojos (district-specific).
- **Jobs/contracts:** Framework jobs pay 1.5×.
- **Equipment/property:** Framework CLI tools.
- **New tech:** Framework architecture, project generators.
- **Rewards/progression:** 25,000 XP.
- **Assessment:** Build a framework app from a bare spec; hidden integration tests.

### Level 18 — Production Debugging
- **Entry:** 25,000 XP.
- **Competencies:** Logs, metrics, reproducing production bugs, incident write-ups. | *Optional:* on-call simulator (NPC pager).
- **Locations/services:** Relay Hub (Systems District).
- **Jobs/contracts:** Incident-response contracts (high pay, time-boxed).
- **Equipment/property:** Observability dashboard.
- **New tech:** Logging, monitoring basics, postmortems.
- **Rewards/progression:** 29,000 XP; title "Specialist".
- **Assessment:** Live incident: diagnose and fix under a time box; hidden fault; write-up graded.

### Level 19 — Performance & Security
- **Entry:** 29,000 XP.
- **Competencies:** Profiling, query optimization, secure coding patterns. | *Optional:* CTF-style side quests.
- **Locations/services:** The Foundry (preview).
- **Jobs/contracts:** Optimization bounties (pay scales with measured improvement).
- **Equipment/property:** Profiler tool.
- **New tech:** Profilers, caching, threat modeling basics.
- **Rewards/progression:** 34,000 XP.
- **Assessment:** Take a slow, vulnerable app; make it 10× faster AND pass the attack script.

### Level 20 — Specialist Capstone
- **Entry:** 34,000 XP.
- **Competencies:** Own a discipline end-to-end; mentor an NPC junior (teaching quest). | *Optional:* publish a framework plugin to the marketplace.
- **Locations/services:** All current cities, full services.
- **Jobs/contracts:** Specialist retainer contracts (recurring income).
- **Equipment/property:** Specialist rig; guild hall access.
- **New tech:** Depth + teaching.
- **Rewards/progression:** 40,000 XP; specialization badge (public, verified).
- **Assessment:** **GATE — Independent + peer:** capstone project, hidden tests, NPC-junior mentoring evaluated; portfolio review.

---

## TIER 5 — THE SYSTEMS DEVELOPER (Levels 21–25)
*Theme: build the machine underneath. Interpreters, concurrency, distributed basics.*

### Level 21 — How Computers Really Work
- **Entry:** L20 gate passed; 40,000 XP.
- **Competencies:** Memory, processes, threads, filesystems (mental models + C exercises). | *Optional:* assembly taster.
- **Locations/services:** Kernel Row (full).
- **Jobs/contracts:** Systems odd-jobs (C), 500+ coins.
- **Equipment/property:** Lab bench access (hardware sim).
- **New tech:** C pointers, memory layout.
- **Rewards/progression:** 46,000 XP.
- **Assessment:** Hidden tests on memory-correct C (leaks/overflows fail via sanitizers).

### Level 22 — Build an Interpreter
- **Entry:** 46,000 XP.
- **Competencies:** Lexing, parsing, evaluation — a working interpreter for a toy language. | *Optional:* add garbage collection.
- **Locations/services:** The Foundry (full).
- **Jobs/contracts:** "Scripting engine for the game studio" (NPC contract).
- **Equipment/property:** Language-lab tools.
- **New tech:** Grammars, ASTs, tree-walk evaluation.
- **Rewards/progression:** 53,000 XP; title "Systems Developer".
- **Assessment:** Hidden language-feature tests (the interpreter must run unseen programs).

### Level 23 — Concurrency
- **Entry:** 53,000 XP.
- **Competencies:** Threads, async, races, locks; prove correctness under stress. | *Optional:* actor-model playground.
- **Locations/services:** Relay Hub (advanced).
- **Jobs/contracts:** "Make the server handle 100× load" (concurrency contract).
- **Equipment/property:** Load-testing rig.
- **New tech:** Concurrency primitives, stress testing.
- **Rewards/progression:** 61,000 XP.
- **Assessment:** Hidden race-injection tests — a racy solution fails deterministically under the harness.

### Level 24 — Build a Database Engine
- **Entry:** 61,000 XP.
- **Competencies:** Storage, indexing, transactions (a tiny working DB). | *Optional:* SQL dialect extension.
- **Locations/services:** Kernel Row (deep lab).
- **Jobs/contracts:** Embedded-DB contract for an NPC startup.
- **Equipment/property:** Dedicated lab server (simulated).
- **New tech:** B-trees (intuition), WAL, ACID.
- **Rewards/progression:** 70,000 XP.
- **Assessment:** Crash-recovery tests (kill -9 mid-transaction; data must survive).

### Level 25 — Company Formation
- **Entry:** 70,000 XP.
- **Competencies:** Found an Indie Studio: hire NPCs, scope a product, ship v1. | *Optional:* take a co-founder (NPC).
- **Locations/services:** Company registry (Startup City); studio office property.
- **Jobs/contracts:** Studio takes small commercial contracts (company ledger).
- **Equipment/property:** **Studio office** (property); hire up to 3 NPC employees.
- **New tech:** Scoping, estimation, team workflows.
- **Rewards/progression:** 80,000 XP; company founded (public registry).
- **Assessment:** **GATE — Independent:** ship the studio's first product with the team; hidden acceptance; postmortem review.

---

## TIER 6 — THE INVENTOR (Levels 26–30)
*Theme: hardware meets software. Embedded, ML from scratch, media.*

### Level 26 — Embedded Awakening
- **Entry:** L25 gate passed; 80,000 XP.
- **Competencies:** Microcontrollers (simulated), GPIO, sensors, C/Rust on tiny targets. | *Optional:* solder-free breadboard sim.
- **Locations/services:** Hardware workshop (Systems District).
- **Jobs/contracts:** Sensor-firmware gigs.
- **Equipment/property:** Dev board kit.
- **New tech:** Embedded C/Rust, datasheets.
- **Rewards/progression:** 90,000 XP.
- **Assessment:** Firmware passes hardware-in-the-loop sim (hidden timing tests).

### Level 27 — Machine Learning from Zero
- **Entry:** 90,000 XP.
- **Competencies:** Math intuition + a neural net written from scratch (no frameworks). | *Optional:* tiny vision project.
- **Locations/services:** ML lab (Systems District).
- **Jobs/contracts:** Data-labeling oversight (ironic, well-paid).
- **Equipment/property:** GPU time-share (simulated).
- **New tech:** Linear algebra intuition, gradient descent.
- **Rewards/progression:** 101,000 XP; title "Inventor".
- **Assessment:** Train a net to a hidden accuracy bar on unseen data (no pretrains).

### Level 28 — Media Processing
- **Entry:** 101,000 XP.
- **Competencies:** Audio/video pipelines: codecs, filters, streaming basics. | *Optional:* chiptune synth project.
- **Locations/services:** Media lab.
- **Jobs/contracts:** "Transcode the archive" (pipeline contract).
- **Equipment/property:** Media workstation.
- **New tech:** DSP intuition, codecs, streaming.
- **Rewards/progression:** 113,000 XP.
- **Assessment:** Pipeline must hit quality + throughput bars on hidden media.

### Level 29 — Protocols & Realtime
- **Entry:** 113,000 XP.
- **Competencies:** Design a small network protocol; realtime constraints. | *Optional:* multiplayer toy game.
- **Locations/services:** Relay Hub (protocol sandbox).
- **Jobs/contracts:** Realtime dashboard contract.
- **Equipment/property:** Protocol analyzer.
- **New tech:** Sockets, protocol design, latency budgets.
- **Rewards/progression:** 126,000 XP.
- **Assessment:** Interop tests — your protocol must talk to a hidden reference implementation.

### Level 30 — Invention Showcase
- **Entry:** 126,000 XP.
- **Competencies:** Combine hardware + software + ML into one invention; demo it. | *Optional:* patent-style write-up (in-fiction).
- **Locations/services:** Invention fair (annual event space).
- **Jobs/contracts:** R&D grants (NPC).
- **Equipment/property:** Inventor's workshop (property).
- **New tech:** Systems integration.
- **Rewards/progression:** 140,000 XP; invention in the public gallery.
- **Assessment:** **GATE — Independent + demo:** working invention, hidden robustness tests, live demo review.

---

## TIER 7 — THE ARCHITECT (Levels 31–35)
*Theme: design at scale. Distributed systems, cloud, secure architecture.*

### Level 31 — Distributed Thinking
- **Entry:** L30 gate passed; 140,000 XP.
- **Competencies:** Consistency, availability, partitions (intuition + sims). | *Optional:* chaos-monkey playground.
- **Locations/services:** Relay Hub (distributed sim).
- **Jobs/contracts:** "Survive the traffic spike" (architecture contract).
- **Equipment/property:** Sim cluster access.
- **New tech:** CAP intuition, replication, consensus (intuition).
- **Rewards/progression:** 155,000 XP.
- **Assessment:** Hidden partition tests — the design must stay correct when the network splits.

### Level 32 — Cloud & Infrastructure
- **Entry:** 155,000 XP.
- **Competencies:** Infra-as-code (simulated), deploys, scaling policies. | *Optional:* cost-optimization game.
- **Locations/services:** Cloud terminal (Systems District).
- **Jobs/contracts:** Migration contracts.
- **Equipment/property:** Cloud credits (in-game).
- **New tech:** IaC concepts, containers, orchestration.
- **Rewards/progression:** 171,000 XP; title "Architect".
- **Assessment:** Deploy a resilient stack from a spec; hidden failure injection during grading.

### Level 33 — Secure Architecture
- **Entry:** 171,000 XP.
- **Competencies:** Threat modeling, defense in depth, authorized pentesting basics. | *Optional:* blue-team sim.
- **Locations/services:** Security range (sandboxed).
- **Jobs/contracts:** Security architecture reviews (high pay).
- **Equipment/property:** Security toolkit.
- **New tech:** Threat models, security boundaries.
- **Rewards/progression:** 188,000 XP.
- **Assessment:** Red-team exercise: your architecture must withstand a scripted attack campaign.

### Level 34 — Data at Scale
- **Entry:** 188,000 XP.
- **Competencies:** Distributed data: sharding, pipelines, exactly-once intuition. | *Optional:* stream-processing project.
- **Locations/services:** Data refinery (Systems District).
- **Jobs/contracts:** Pipeline contracts (recurring).
- **Equipment/property:** Data cluster time.
- **New tech:** Distributed data patterns.
- **Rewards/progression:** 206,000 XP.
- **Assessment:** Hidden data-loss/duplication tests under failure injection.

### Level 35 — Enterprise Design Review
- **Entry:** 206,000 XP.
- **Competencies:** Lead a design review; defend trade-offs; plan a migration. | *Optional:* mentor two NPC juniors.
- **Locations/services:** Boardroom (Innovation Capital preview, visit only).
- **Jobs/contracts:** Enterprise architecture contract (largest yet).
- **Equipment/property:** Architect's studio.
- **New tech:** Technical leadership.
- **Rewards/progression:** 225,000 XP; company may upgrade to Software Company.
- **Assessment:** **GATE — Independent + panel:** present an architecture to an NPC panel; hidden scenario changes mid-review (adaptability graded).

---

## TIER 8 — THE INNOVATOR (Levels 36–40)
*Theme: original contributions. Innovation Capital opens. Compilers, OS, AI.*

### Level 36 — Arrival at the Capital
- **Entry:** L35 gate passed; 225,000 XP.
- **Competencies:** Establish a research agenda; pick an innovation track. | *Optional:* tour the Capital.
- **Locations/services:** **Innovation Capital** (entry district).
- **Jobs/contracts:** Research grants.
- **Equipment/property:** Research lab access.
- **New tech:** Research methods for engineers.
- **Rewards/progression:** 245,000 XP; arrival honors.
- **Assessment:** Research proposal review (novelty + feasibility rubric).

### Level 37 — Build a Compiler (or Small OS)
- **Entry:** 245,000 XP.
- **Competencies:** A real compiler (or a tiny working OS) — choose one. | *Optional:* target the game's toy VM.
- **Locations/services:** Foundry deep labs.
- **Jobs/contracts:** Toolchain contracts.
- **Equipment/property:** Dedicated build farm (simulated).
- **New tech:** Codegen / kernel basics.
- **Rewards/progression:** 266,000 XP; title "Innovator".
- **Assessment:** Hidden conformance suite (compiler) or boot+syscall tests (OS).

### Level 38 — Train & Deploy Real AI
- **Entry:** 266,000 XP.
- **Competencies:** Train a small model on a real task; deploy it behind an API with evals. | *Optional:* model card + bias review.
- **Locations/services:** AI foundry.
- **Jobs/contracts:** ML deployment contracts.
- **Equipment/property:** Training cluster allocation.
- **New tech:** Training loops, evals, deployment.
- **Rewards/progression:** 288,000 XP.
- **Assessment:** Hidden eval set; latency/cost budget enforced.

### Level 39 — Protocols & Engines
- **Entry:** 288,000 XP.
- **Competencies:** Ship a networking protocol or a small game engine used by others. | *Optional:* open-source it in-fiction.
- **Locations/services:** Capital workshops.
- **Jobs/contracts:** Protocol adoption bounties.
- **Equipment/property:** Engine lab.
- **New tech:** Public-interface design.
- **Rewards/progression:** 311,000 XP.
- **Assessment:** Adoption test: NPC teams must successfully build on your protocol/engine.

### Level 40 — Lead Something Real
- **Entry:** 311,000 XP.
- **Competencies:** Lead a multi-NPC project: planning, reviews, shipping. | *Optional:* conference talk (in-fiction).
- **Locations/services:** Full Capital access.
- **Jobs/contracts:** Lead contracts (team-based).
- **Equipment/property:** Leadership office; company may become Technology Enterprise.
- **New tech:** Engineering leadership.
- **Rewards/progression:** 335,000 XP; leadership commendation.
- **Assessment:** **GATE — Independent + 360:** ship with the team; hidden quality bar; NPC peer reviews.

---

## TIER 9 — THE MASTER ENGINEER (Levels 41–45)
*Theme: judgment, originality, mentorship. Trade-offs and taste.*

### Level 41 — Resilient by Design
- **Entry:** L40 gate passed; 335,000 XP.
- **Competencies:** Design for failure as the default; SLOs, error budgets. | *Optional:* write the district's resilience handbook.
- **Locations/services:** Capital (all).
- **Jobs/contracts:** Resilience audits (top pay).
- **Equipment/property:** Chaos lab.
- **New tech:** SLOs, graceful degradation.
- **Rewards/progression:** 360,000 XP.
- **Assessment:** Hidden cascading-failure scenarios; budget math checked.

### Level 42 — Original Tools
- **Entry:** 360,000 XP.
- **Competencies:** Build an original tool/framework/infra piece others adopt. | *Optional:* documentation as a craft quest.
- **Locations/services:** Toolworks.
- **Jobs/contracts:** Tooling grants.
- **Equipment/property:** Toolworks studio.
- **New tech:** Developer-experience design.
- **Rewards/progression:** 386,000 XP; title "Master Engineer".
- **Assessment:** Adoption + hidden usability tests (can a stranger succeed with only your docs?).

### Level 43 — Cross-Domain Synthesis
- **Entry:** 386,000 XP.
- **Competencies:** Combine AI + hardware + software + security in one system. | *Optional:* cross-mentor an NPC from another discipline.
- **Locations/services:** Synthesis labs.
- **Jobs/contracts:** Flagship synthesis contracts.
- **Equipment/property:** Synthesis lab (property).
- **New tech:** Systems judgment.
- **Rewards/progression:** 413,000 XP.
- **Assessment:** Hidden integration faults; the whole must exceed the sum.

### Level 44 — The Mentor
- **Entry:** 413,000 XP.
- **Competencies:** Mentor NPC juniors to their own capstones; teaching as engineering. | *Optional:* write a curriculum.
- **Locations/services:** Academy (Capital).
- **Jobs/contracts:** Mentorship stipends.
- **Equipment/property:** Academy chair.
- **New tech:** Pedagogy for engineers.
- **Rewards/progression:** 441,000 XP.
- **Assessment:** Your mentees' independent assessments are your assessment (skin in the game).

### Level 45 — Masterwork
- **Entry:** 441,000 XP.
- **Competencies:** A masterwork: the best thing you've ever built, judged by masters. | *Optional:* public exhibition.
- **Locations/services:** Grand gallery.
- **Jobs/contracts:** Masterwork commissions.
- **Equipment/property:** Master studio.
- **New tech:** Taste.
- **Rewards/progression:** 470,000 XP; masterwork enshrined.
- **Assessment:** **GATE — Independent + masters' panel:** hidden criteria revealed only at judgment (true mastery has no study guide).

---

## TIER 10 — THE CREATOR (Levels 46–50)
*Theme: legacy. Original technology, companies, open-ended creation.*

### Level 46 — The Company
- **Entry:** L45 gate passed; 470,000 XP.
- **Competencies:** Found or lead a Technology Enterprise: vision, hiring, runway. | *Optional:* pivot drill.
- **Locations/services:** Enterprise district.
- **Jobs/contracts:** Enterprise contracts (largest scale).
- **Equipment/property:** Headquarters (property).
- **New tech:** Organizational design.
- **Rewards/progression:** 500,000 XP; enterprise registered.
- **Assessment:** 4-quarter simulation: survive hidden market shocks.

### Level 47 — Original Technology
- **Entry:** 500,000 XP.
- **Competencies:** Create an original database, language, engine, or OS — a real contribution. | *Optional:* specification document.
- **Locations/services:** Creation labs.
- **Jobs/contracts:** Research endowments.
- **Equipment/property:** Creation lab (property).
- **New tech:** Original research + engineering.
- **Rewards/progression:** 532,000 XP; title "Creator".
- **Assessment:** Hidden expert review (novelty, correctness, utility).

### Level 48 — The Community
- **Entry:** 532,000 XP.
- **Competencies:** Lead an open-source community or company: governance, releases, culture. | *Optional:* foundation charter.
- **Locations/services:** Community commons.
- **Jobs/contracts:** Foundation grants.
- **Equipment/property:** Commons hall.
- **New tech:** Community leadership.
- **Rewards/progression:** 565,000 XP.
- **Assessment:** Community health metrics over a simulated year (hidden events).

### Level 49 — The Frontier
- **Entry:** 565,000 XP.
- **Competencies:** Open-ended frontier challenges: problems with no known good answer. | *Optional:* collaborate with rival NPC creators.
- **Locations/services:** Frontier (beyond the Capital).
- **Jobs/contracts:** Frontier bounties (unspecified outcomes).
- **Equipment/property:** Frontier outpost.
- **New tech:** Unknown.
- **Rewards/progression:** 600,000 XP.
- **Assessment:** Peer judgment by creators (no hidden tests — judgment replaces measurement at the frontier).

### Level 50 — Creator
- **Entry:** 600,000 XP; all prior gates passed.
- **Competencies:** Everything, synthesized. The final trial is a public work judged by the whole world. | *Optional:* choose a successor NPC to mentor.
- **Locations/services:** Everywhere; the world is yours.
- **Jobs/contracts:** Any; legends choose.
- **Equipment/property:** Creator's spire.
- **New tech:** Whatever you invent next.
- **Rewards/progression:** Title **Creator**; name in the world's founding record; New Game+ (mentor mode) unlocked.
- **Assessment:** **FINAL GATE:** a public masterwork, judged openly against the standard you spent 50 levels defining. No hidden tests — at this point, *you* are the standard.

---

## Tuning notes

- XP numbers are curve illustrations; final values come from playtest
  medians ([TEST] §3). The invariant is *gate assessments are real* at
  every tier boundary (5, 10, 15, 20, 25, 30, 35, 40, 45, 50).
- Phase 1 ships Tier 1 fully + Tier 2 partially (~10 missions total per
  [PRD] §4). Tiers 3–10 are specified here so Phase 1 architecture
  (gating, evidence, economy) is built against the real shape.
- Skill-tree nodes ([GDD] §3) cross-reference levels: each level's
  "Competencies" maps to node IDs in the content registry (content task,
  not duplicated here).
