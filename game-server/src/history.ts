// history.ts — the append-only side of the game (ClickHouse) behind one
// interface, so handlers can be tested against an in-memory implementation.
//
// Writes go to the immutable tables; reads come from the materialized views
// (`mv_player_xp`, `mv_player_balance`, `mv_mission_pass`). Nothing here is ever
// updated in place.

import { ClickHouse } from "./clickhouse";

export interface GameEvent {
  playerId: string;
  event: string;
  sessionId?: string;
  service?: string;
  props?: Record<string, unknown>;
  ts?: string;
}

export interface XpEventRow {
  playerId: string;
  source: string;
  amount: number;
  ref?: string;
  levelAfter?: number;
  ts?: string;
}

export interface LedgerRow {
  ref: string;
  playerId: string;
  account: string;
  amount: number;
  currency?: string;
  memo?: string;
  ts?: string;
}

export interface MissionAttemptRow {
  playerId: string;
  missionId: string;
  runtime: string;
  codeHash?: string;
  passed: boolean;
  publicPassed?: number;
  hiddenPassed?: number;
  total?: number;
  durationMs?: number;
  ts?: string;
}

export interface AssessmentRow {
  requestId: string;
  runtime: string;
  passed: number;
  total: number;
  durationMs?: number;
  timedOut?: boolean;
  errorKind?: string;
  ts?: string;
}

export interface MissionStat {
  missionId: string;
  attempts: number;
  passes: number;
}

export interface EventSink {
  insertEvents(rows: GameEvent[]): Promise<void>;
  insertXp(rows: XpEventRow[]): Promise<void>;
  insertLedger(rows: LedgerRow[]): Promise<void>;
  insertMissionAttempt(row: MissionAttemptRow): Promise<void>;
  insertAssessment(row: AssessmentRow): Promise<void>;
  playerTotalXp(playerId: string): Promise<number>;
  playerBalance(playerId: string, currency?: string): Promise<number>;
  missionStats(): Promise<MissionStat[]>;
  ping(): Promise<boolean>;
}

const num = (v: unknown): number => (v == null ? 0 : Number(v));

/** camelCase → snake_case, so TS field names map onto ClickHouse columns. */
const snake = (s: string): string => s.replace(/[A-Z]/g, (c) => "_" + c.toLowerCase());

/**
 * Build a JSONEachRow column object: camelCase keys become the table's
 * snake_case columns and undefined values are omitted (so ClickHouse column
 * defaults apply). ClickHouse silently ignores unknown JSON fields, so this
 * mapping is what stops us writing rows with empty player_id etc.
 */
function cols<T extends object>(o: T): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(o)) if (v !== undefined) out[snake(k)] = v;
  return out;
}

/** ClickHouse-backed history. */
export class ClickHouseHistory implements EventSink {
  constructor(private readonly ch: ClickHouse) {}

  async insertEvents(rows: GameEvent[]): Promise<void> {
    await this.ch.insert(
      "events",
      rows.map((r) => cols({ ...r, props: JSON.stringify(r.props ?? {}) })),
    );
  }

  async insertXp(rows: XpEventRow[]): Promise<void> {
    await this.ch.insert("xp_events", rows.map((r) => cols(r)));
  }

  async insertLedger(rows: LedgerRow[]): Promise<void> {
    await this.ch.insert(
      "ledger_entries",
      rows.map((r) => cols({ ...r, currency: r.currency ?? "credits" })),
    );
  }

  async insertMissionAttempt(row: MissionAttemptRow): Promise<void> {
    await this.ch.insert("mission_attempts", [
      cols({
        ...row,
        passed: row.passed ? 1 : 0,
        publicPassed: row.publicPassed ?? 0,
        hiddenPassed: row.hiddenPassed ?? 0,
        total: row.total ?? 0,
        durationMs: row.durationMs ?? 0,
        codeHash: row.codeHash ?? "",
      }),
    ]);
  }

  async insertAssessment(row: AssessmentRow): Promise<void> {
    await this.ch.insert("assessment_events", [
      cols({
        ...row,
        durationMs: row.durationMs ?? 0,
        timedOut: row.timedOut ? 1 : 0,
        errorKind: row.errorKind ?? "",
      }),
    ]);
  }

  async playerTotalXp(playerId: string): Promise<number> {
    const row = await this.ch.queryRow<{ xp: unknown }>(
      "SELECT sum(xp) AS xp FROM mv_player_xp WHERE player_id = {p:String}",
      { p: playerId },
    );
    return num(row?.xp);
  }

  async playerBalance(playerId: string, currency = "credits"): Promise<number> {
    const row = await this.ch.queryRow<{ b: unknown }>(
      "SELECT sum(balance) AS b FROM mv_player_balance WHERE player_id = {p:String} AND account = 'wallet' AND currency = {c:String}",
      { p: playerId, c: currency },
    );
    return num(row?.b);
  }

  async missionStats(): Promise<MissionStat[]> {
    const rows = await this.ch.query<{ mission_id: string; attempts: unknown; passes: unknown }>(
      "SELECT mission_id, countMerge(attempts) AS attempts, sumMerge(passes) AS passes FROM mv_mission_pass GROUP BY mission_id ORDER BY mission_id",
    );
    return rows.map((r) => ({
      missionId: r.mission_id,
      attempts: num(r.attempts),
      passes: num(r.passes),
    }));
  }

  ping(): Promise<boolean> {
    return this.ch.ping();
  }
}


/** In-memory history for unit tests; mirrors the aggregates the views provide. */
export class MemoryHistory implements EventSink {
  readonly events: GameEvent[] = [];
  readonly xp: XpEventRow[] = [];
  readonly ledger: LedgerRow[] = [];
  readonly attempts: MissionAttemptRow[] = [];
  readonly assessments: AssessmentRow[] = [];
  healthy = true;

  async insertEvents(rows: GameEvent[]) {
    this.events.push(...rows);
  }
  async insertXp(rows: XpEventRow[]) {
    this.xp.push(...rows);
  }
  async insertLedger(rows: LedgerRow[]) {
    this.ledger.push(...rows);
  }
  async insertMissionAttempt(row: MissionAttemptRow) {
    this.attempts.push(row);
  }
  async insertAssessment(row: AssessmentRow) {
    this.assessments.push(row);
  }
  async playerTotalXp(playerId: string) {
    return this.xp.filter((r) => r.playerId === playerId).reduce((a, r) => a + r.amount, 0);
  }
  async playerBalance(playerId: string, currency = "credits") {
    return this.ledger
      .filter((r) => r.playerId === playerId && r.account === "wallet" && (r.currency ?? "credits") === currency)
      .reduce((a, r) => a + r.amount, 0);
  }
  async missionStats(): Promise<MissionStat[]> {
    const by = new Map<string, MissionStat>();
    for (const a of this.attempts) {
      const s = by.get(a.missionId) ?? { missionId: a.missionId, attempts: 0, passes: 0 };
      s.attempts++;
      if (a.passed) s.passes++;
      by.set(a.missionId, s);
    }
    return [...by.values()].sort((x, y) => x.missionId.localeCompare(y.missionId));
  }
  async ping() {
    return this.healthy;
  }
}
