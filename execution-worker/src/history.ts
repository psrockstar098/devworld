/**
 * Event sink: the worker's write path into ClickHouse (the main database).
 *
 * Grading is a fire-and-forget concern for storage: if ClickHouse is slow or
 * down, assessment must still return. Every method here is therefore called
 * with a `.catch()` by the caller, and `NullEvents` is the default when no
 * ClickHouse is configured (tests, local runs without infra).
 *
 * Column names are snake_case to match the schema in infra/clickhouse/init.
 */

import { ClickHouse } from "./clickhouse";

export interface AssessmentEvent {
  requestId: string;
  runtime: string;
  passed: number;
  total: number;
  durationMs?: number;
  timedOut?: boolean;
  errorKind?: string;
}

export interface ModerationSignalRow {
  playerId?: string;
  kind: string;
  detail?: string;
  count?: number;
}

export interface EventSink {
  recordAssessment(event: AssessmentEvent): Promise<void>;
  recordSignals(rows: ModerationSignalRow[]): Promise<void>;
  ping(): Promise<boolean>;
}

/** Default sink: discards everything. Keeps the worker dependency-free. */
export class NullEvents implements EventSink {
  async recordAssessment(_event: AssessmentEvent): Promise<void> {}
  async recordSignals(_rows: ModerationSignalRow[]): Promise<void> {}
  async ping(): Promise<boolean> {
    return true;
  }
}

const snake = (s: string): string => s.replace(/[A-Z]/g, (c) => "_" + c.toLowerCase());

function cols(o: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(o)) if (v !== undefined) out[snake(k)] = v;
  return out;
}

export class ClickHouseEvents implements EventSink {
  constructor(private readonly ch: ClickHouse) {}

  async recordAssessment(e: AssessmentEvent): Promise<void> {
    await this.ch.insert("assessment_events", [
      cols({
        requestId: e.requestId,
        runtime: e.runtime,
        passed: e.passed,
        total: e.total,
        durationMs: e.durationMs ?? 0,
        timedOut: e.timedOut ? 1 : 0,
        errorKind: e.errorKind ?? "",
      }),
    ]);
  }

  async recordSignals(rows: ModerationSignalRow[]): Promise<void> {
    if (rows.length === 0) return;
    await this.ch.insert(
      "moderation_signals",
      rows.map((r) =>
        cols({
          playerId: r.playerId ?? "",
          kind: r.kind,
          detail: r.detail ?? "",
          count: r.count ?? 0,
        }),
      ),
    );
  }

  ping(): Promise<boolean> {
    return this.ch.ping();
  }
}
