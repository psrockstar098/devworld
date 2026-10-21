# DEVWORLD Stage 2-5 Gap Audit — 2026-10-21 (audited by D, direct read of repo at 454b185)

## DONE (evidence)
- Stage 2 skeleton: 4 cities in world.json (Starter Town Lv1, Startup City Lv6, Systems District Lv16, Innovation Capital Lv36) with travel links + fares; interiors per city; movement/camera verified in play test; day/night + ambience in shell (game.h/game_logic/regions); HUD live (level, XP, credits, quests, minimap); character creation; profile.cpp, equipment.cpp, housing.cpp, company.cpp, careers.cpp (5 paths), regions.cpp, rewards.cpp, settings.cpp, social.cpp, mastery.cpp, portfolio.cpp, unlocks.cpp (14 unlocks, 4 milestones), achievements (15).
- Stage 3: real VS Code workbench live; wasm runners in-browser; execution-worker with 18 runtimes + /execute + /assess.
- Stage 4: curriculum.json (12 areas, 38 topics); 23 missions with hidden tests; mastery.cpp.
- Stage 5: progression engine, MAX_LEVEL 50 with tier titles; unlock logic; rewards.

## MISSING / PARTIAL (build these, in priority order for the 1pm push)
1. MULTIPLAYER PRESENCE (highest priority — the "real and dynamic" core): zero presence/friend/neighbor/roommate/messaging code in game-server/src. Shell social.cpp exists but has no server counterpart. Build: presence (online players, locations), neighbor/roommate assignment between REAL player ids, friend requests, player messaging, shared rooms. Server-authoritative, persistent. No fake players anywhere.
2. NPC LIFE: 8 NPCs have schedule + static dialogue nodes only. Missing: memory (jobs, promises, help, conversations), relationships that change responses, careers/ambitions (study, apply, promote, found, mentor), needs/limits (time, budget, energy), independent decisions (quit, side project, decline unfit jobs), event-driven dialogue from a world event log.
3. ECONOMY CORE: no supply/demand pricing, no product lifecycle engine, no company cash/failure simulation on the server (ledger tracks balances only). Build: demand-driven prices, product lifecycle stages, company cashflow + failure/restructure paths, tech shifts re-pricing skills.
4. OFFLINE TIME + EVENT LOG: no bounded offline catch-up (rent/salaries/ops while away, capped and fair) and no world event log feeding a newspaper/timeline. Build both; newspaper + timeline read the same log.
5. AFTER 1PM (do not start before 1-4 are done): endgame systems (guilds, M&A, SaaS, investor funding, hackathons, retro engineering, logic-gate computer, city funding, invention lineage, world-changing inventions).

## RULES (non-negotiable)
- Real players only: no bot players, no fake seed accounts, no fabricated activity. NPCs are clearly-marked world characters, never posing as players.
- Living society north star: systems must have causes and consequences, not decoration. If it only looks pretty or hands out a quest, give it a real function or cut it.
- Server authoritative for valuable/competitive state (presence, relationships, economy, progression).
- Do NOT touch auth/OAuth code — another session owns that. Do NOT push.
