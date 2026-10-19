-- DEVWORLD ClickHouse schema — materialized views.
--
-- These turn the append-only tables into cheap current-state reads without ever
-- mutating history: balances, XP totals and per-mission pass rates.

-- Balance per player/account/currency, maintained incrementally from the ledger.
-- Keyed by account as well as player: a wallet credit and its `system`
-- counter-entry belong to the same player and would cancel out otherwise.
-- Read a wallet with `WHERE account = 'wallet'`.
CREATE MATERIALIZED VIEW IF NOT EXISTS devworld.mv_player_balance
ENGINE = SummingMergeTree
PARTITION BY currency
ORDER BY (player_id, account, currency)
AS SELECT player_id, account, currency, sum(amount) AS balance
   FROM devworld.ledger_entries
   GROUP BY player_id, account, currency;

-- Lifetime XP per player (level is derived from this).
CREATE MATERIALIZED VIEW IF NOT EXISTS devworld.mv_player_xp
ENGINE = SummingMergeTree
ORDER BY player_id
AS SELECT player_id, sum(amount) AS xp
   FROM devworld.xp_events
   GROUP BY player_id;

-- Attempts and passes per mission, for pass-rate / difficulty dashboards.
CREATE MATERIALIZED VIEW IF NOT EXISTS devworld.mv_mission_pass
ENGINE = AggregatingMergeTree
ORDER BY mission_id
AS SELECT mission_id,
          countState()      AS attempts,
          sumState(passed)  AS passes
   FROM devworld.mission_attempts
   GROUP BY mission_id;
