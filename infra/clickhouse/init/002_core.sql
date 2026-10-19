-- DEVWORLD ClickHouse schema — the append-only main database.
--
-- ClickHouse is the system of record for *history*: what happened, when, to whom.
-- Mutable current-state (a player's inventory, the live session) lives in the
-- RocksDB store and Redis instead. Everything here is append-only, which is what
-- makes ledger and XP math auditable: balances are derived, never overwritten.

-- Generic event stream: the catch-all sink for gameplay + service telemetry.
CREATE TABLE IF NOT EXISTS devworld.events
(
    ts         DateTime64(3, 'UTC') DEFAULT now64(3),
    player_id  String,
    session_id String DEFAULT '',
    event      LowCardinality(String),
    service    LowCardinality(String) DEFAULT '',
    props      String DEFAULT '{}'          -- JSON object as text
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (event, player_id, ts)
TTL ts + INTERVAL 180 DAY;

-- Double-entry ledger. Every row belongs to a transaction `ref`; the rows of a
-- transaction must sum to zero. Balances are always derived (see the MV below).
CREATE TABLE IF NOT EXISTS devworld.ledger_entries
(
    entry_id  UUID DEFAULT generateUUIDv4(),
    ts        DateTime64(3, 'UTC') DEFAULT now64(3),
    ref       String,                       -- transaction id, shared by its rows
    player_id String,
    account   LowCardinality(String),       -- wallet | escrow | housing | system
    amount    Int64,                        -- signed; per ref, SUM(amount) = 0
    currency  LowCardinality(String) DEFAULT 'credits',
    memo      String DEFAULT ''
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (ref, player_id, account, ts);

-- XP / progression events (append-only; level is derived from the total).
CREATE TABLE IF NOT EXISTS devworld.xp_events
(
    ts          DateTime64(3, 'UTC') DEFAULT now64(3),
    player_id   String,
    source      LowCardinality(String),     -- mission | job | exploration | ...
    ref         String DEFAULT '',
    amount      Int64,
    level_after UInt16 DEFAULT 0
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (player_id, ts);

-- One row per mission grading attempt.
CREATE TABLE IF NOT EXISTS devworld.mission_attempts
(
    ts            DateTime64(3, 'UTC') DEFAULT now64(3),
    player_id     String,
    mission_id    LowCardinality(String),
    runtime       LowCardinality(String),
    code_hash     String DEFAULT '',
    passed        UInt8,                    -- 1 when *all* tests pass
    public_passed UInt16 DEFAULT 0,
    hidden_passed UInt16 DEFAULT 0,
    total         UInt16 DEFAULT 0,
    duration_ms   UInt32 DEFAULT 0
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (mission_id, player_id, ts);

-- Raw execution-worker assessment events (one per /assess call).
CREATE TABLE IF NOT EXISTS devworld.assessment_events
(
    ts         DateTime64(3, 'UTC') DEFAULT now64(3),
    request_id String,
    runtime    LowCardinality(String),
    passed     UInt16,
    total      UInt16,
    duration_ms UInt32 DEFAULT 0,
    timed_out  UInt8 DEFAULT 0,
    error_kind LowCardinality(String) DEFAULT ''
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (runtime, ts);

-- Advisory abuse signals from the execution worker's moderation feed.
CREATE TABLE IF NOT EXISTS devworld.moderation_signals
(
    ts        DateTime64(3, 'UTC') DEFAULT now64(3),
    player_id String DEFAULT '',
    kind      LowCardinality(String),
    detail    String DEFAULT '',
    count     UInt32 DEFAULT 0
)
ENGINE = MergeTree
PARTITION BY toYYYYMM(ts)
ORDER BY (kind, ts);
