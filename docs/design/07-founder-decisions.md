# DEVWORLD — Founder Decisions

One page, 11 calls. Each row: what you pick, what it unlocks, when it must be
picked. Nothing here is decided for you — mark each `[ ]` with your call and the
build continues. Items marked **now** block current work; the rest can wait until
their stated gate.

## Decide now (block current or near-term work)

| # | Decision | Options (plain) | What it unlocks | Gate |
|---|----------|-----------------|-----------------|------|
| OPEN-1 | **Monetization model** | a) Free + cosmetics (skins, town decorations) b) Subscription (monthly pro tier) c) Grant/sponsor funded (OSS programs) | Phase 2 economy tuning; whether any payment code gets written at all | Before Phase 2 economy work |
| OPEN-3 | **Age rating / child-safety posture** | a) 13+ with basic protections b) All-ages incl. under-16 (needs COPPA/GDPR-K legal review) | Account system design, chat/DM rules, data retention | Before public beta |
| OPEN-W1 | **District art direction per city** | Vibe words are locked — this is the tile/palette art pass per district | First art milestone; the look of each district | First art milestone |

## Decide later (locked to their gates)

| # | Decision | Closes when |
|---|----------|-------------|
| OPEN-2 | Minimum device spec (phone/browser matrix) | After Phase 1 performance profiling |
| OPEN-4 | Real-money marketplace — Phase 3 "genuine paid work" | Separate product track; explicitly **not** designed now |
| OPEN-W2 | Phase 3 cities beyond the Capital (Harbor/Frontier) | When Phase 3 scoping starts |
| OPEN-A1 | Hosting topology: single-region vs multi-region, execution-host fleet sizing | After Phase 1 load testing |
| OPEN-A2 | Realtime: raw WebSocket + Redis self-hosted vs managed realtime service | Phase 2 planning |
| OPEN-A3 | Data retention policy (chat/AI/forensics TTLs) | With OPEN-3 legal review |
| OPEN-T1 | Load-test targets (concurrent players, submissions/sec) | After OPEN-A1 hosting decision |
| OPEN-T2 | Accessibility audit scope (WCAG target, audit vendor) | Before public beta |

## Notes

- OPEN-1 leaning cosmetic-only or grant-funded keeps the Phase 1 build clean of
  payment code; subscription can be bolted on later without redesign.
- OPEN-3 is the only one with legal exposure — if you want under-16 players,
  get the legal review early, not at beta.
- OPEN-4 stays parked: no design work happens on paid-work mechanics until you
  explicitly open that track.
