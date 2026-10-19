# DEVWORLD — World Specification

**Version:** 0.1 (2026-10-19)
**Map philosophy:** cozy 2D, colorful, readable at a glance. The world is a
chapter book: each city is a chapter, each district a section. You can *see*
a locked city across the water long before you can reach it — aspiration is
part of the design.

---

## 1. The map (overview)

```
                        ┌─────────────────────┐
                        │  INNOVATION CAPITAL │  L36+ (Phase 3)
                        │  research, elite    │
                        └────────┬────────────┘
                                 │ (merit bridge)
              ┌──────────────────┴──────────────────┐
              │           SYSTEMS DISTRICT          │  L16–35
              │  Kernel Row · Foundry · Relay Hub   │
              └────────┬───────────────────┬────────┘
                       │                   │
        ┌──────────────┴─────┐   ┌─────────┴──────────────┐
        │   STARTUP CITY     │   │  (Phase 3: Harbor etc.)│
        │  L6–20             │   └────────────────────────┘
        └────────┬───────────┘
                 │ (the long road — on foot first, then bus)
        ┌────────┴───────────┐
        │   STARTER TOWN     │  L1–5  ← every player begins here
        │  (Phase 1: fully built)
        └────────────────────┘
```

Phase 1 builds **Starter Town only**. Other cities are defined here so Phase 1
systems (gating, travel, services) are built against the real shape.

## 2. Competency gating (how unlocking works)

A city or district unlocks on **demonstrated competency, not level number
alone**. Each gate lists required skill-tree nodes ([GDD] §3) and/or an
independent assessment. Level is a *necessary but not sufficient* condition.

- Gate evaluation is server-side ([ARCH] §5).
- When a gate is one node away, the map shows a "nearly there" hint naming
  the exact missing competency — never a vague "keep playing".
- Failing closed: if the gating service disagrees or is down, the gate stays
  locked and the event is logged (never strand, never wrongly open).

## 3. STARTER TOWN (L1–5, Phase 1 — fully built)

Vibe: warm dusk, string lights, a repair-shop street. Everything within
walking distance; the whole town is the tutorial without feeling like one.

| District / POI | Services | Notes |
|---|---|---|
| **Home Row** | Player room (starter bunk), mailbox (mission notices) | Fallback spawn point |
| **The Computer Lab** | The in-game IDE terminals; free practice machines | Where missions are *taken* |
| **Mission Board Plaza** | Mission board (jobs L1–5), notice wall | Core loop anchor |
| **Fix-It Shop (NPC: Mara)** | Repair jobs, first paid work, tool purchases | Python path intro |
| **Data Kiosk (NPC: Old Ben)** | SQL playground, dataset bounties | SQL path intro |
| **Market Street** | Shops (equipment, snacks as fun consumables), bank (wallet UI) | Economy tutorial |
| **Bus Stop** | Travel (locked until L6 — visible, aspirational) | Teases Startup City |

Starter Town rules: no rent (safety net), no PvP-adjacent systems, all
missions completable with the starter computer.

## 4. STARTUP CITY (L6–20, Phase 2)

Vibe: bright morning, glass and murals, busy. The "get a real job" city.

| District | Theme | Key services |
|---|---|---|
| **Founders' Row** | Teams & startups | Team formation office, junior job board |
| **Marketplace Mile** | Commerce | Project listings, template stalls (project sales §10) |
| **Hackathon Hall** | Events | Scheduled competitions, team missions |
| **Apartment Blocks** | Housing | First rentable properties (§6) |

**Gate (competency):** L6 + "debugging basics" node + L5 gate assessment.
Districts inside unlock progressively (Founders' Row at entry; Hackathon Hall
at L11 + "testing" node).

## 5. SYSTEMS DISTRICT (L16–35, Phase 2–3)

Vibe: night, neon on dark steel, hum of servers. For builders of deep things.

| District | Theme | Key services |
|---|---|---|
| **Kernel Row** | OS / low-level | C/Rust questlines, hardware lab |
| **The Foundry** | Compilers / languages | Language-design challenges |
| **Relay Hub** | Networks / infra | Networking sim missions, server rentals |

**Gate (competency):** L16 + "C memory management" node + data-structures
capstone. This is the first gate most players will *feel* — it must be hard
but fair, with a visible preparation track.

## 6. INNOVATION CAPITAL (L36+, Phase 3)

Vibe: dawn over white towers. Research partnerships, elite contracts,
company headquarters, the creator marketplace. Defined at low detail until
Phase 3 — the map reserves the space and the gate shape.

**Gate (competency):** L36 + two specialization capstones + "systems design"
independent assessment.

## 7. Travel

- **Within a city:** walk (free, the intended experience) + instant
  fast-travel between discovered POIs.
- **Between cities:** bus/ferry with a small in-game fee (money sink);
  first arrival in a new city is free and cinematic.
- **Discovery:** POIs unlock fast-travel only after first physical visit.
- **Failure:** travel during a server hiccup → stay put, no fee charged,
  clear message (idempotent travel tokens).

## 8. Services matrix (who gets what, where)

| Service | Starter Town | Startup City | Systems District | Innovation Capital |
|---|---|---|---|---|
| Mission board | L1–5 missions | L6–20 | L16–35 | L36+ |
| Job board | odd jobs | junior jobs | contracts | elite contracts |
| Shops/equipment | basics | pro gear | lab hardware | research rigs |
| Housing | starter bunk (free) | rentals | — | estates (P3) |
| Bank/wallet | yes | yes | yes | yes |
| Social spaces | plaza | guild halls | — | summit (P3) |
| AI mentor | yes | yes | yes | yes |

## 9. World simulation notes (for [ARCH] §5)

- Time: the world has a gentle day/night cycle (cosmetic) + a weekly
  rhythm (hackathons on weekends, market restock Mondays).
- NPCs: quest-givers and shopkeepers with fixed personalities; no
  simulated full lives in Phase 1 (scope guard).
- Persistence: player-visible world state (shop stock, notice boards) is
  per-player instanced in Phase 1; shared world state arrives with
  multiplayer (Phase 2).

## OPEN

- **OPEN-W1 — District art direction per city:** vibe words are locked;
  tile/palette guides need the art pass (closes with first art milestone).
- **OPEN-W2 — Phase 3 cities beyond the Capital:** Harbor/Frontier concepts
  exist only as names; closed when Phase 3 scoping starts.
