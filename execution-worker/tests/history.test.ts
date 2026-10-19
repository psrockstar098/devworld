import { describe, expect, test } from "bun:test";
import { ClickHouse, ClickHouseError, quoteTable, type FetchLike } from "../src/clickhouse";
import { ClickHouseEvents, NullEvents } from "../src/history";

function capture() {
  const bodies: string[] = [];
  const fetchImpl: FetchLike = async (_url, init) => {
    if (init?.body) bodies.push(init.body);
    return new Response("", { status: 200 });
  };
  const ch = new ClickHouse({ url: "http://ch:8123", user: "u", password: "p", database: "devworld" }, fetchImpl);
  return { events: new ClickHouseEvents(ch), bodies };
}

const firstRow = (body: string): Record<string, unknown> =>
  JSON.parse(body.split("\n").find((l) => l.startsWith("{")) ?? "{}");

describe("quoteTable", () => {
  test("quotes valid names and rejects injection", () => {
    expect(quoteTable("assessment_events")).toBe("`assessment_events`");
    expect(quoteTable("devworld.events")).toBe("`devworld`.`events`");
    expect(() => quoteTable("x; DROP TABLE y")).toThrow(ClickHouseError);
  });
});

describe("ClickHouseEvents", () => {
  test("recordAssessment writes snake_case columns and 0/1 booleans", async () => {
    const { events, bodies } = capture();
    await events.recordAssessment({ requestId: "r1", runtime: "python", passed: 2, total: 3, durationMs: 42, timedOut: false, errorKind: "wrong-output" });
    expect(bodies[0]).toContain("INSERT INTO `assessment_events`");
    expect(firstRow(bodies[0])).toEqual({
      request_id: "r1",
      runtime: "python",
      passed: 2,
      total: 3,
      duration_ms: 42,
      timed_out: 0,
      error_kind: "wrong-output",
    });
  });

  test("missing optionals become column defaults", async () => {
    const { events, bodies } = capture();
    await events.recordAssessment({ requestId: "r2", runtime: "sql", passed: 0, total: 1 });
    const row = firstRow(bodies[0]);
    expect(row.duration_ms).toBe(0);
    expect(row.timed_out).toBe(0);
    expect(row.error_kind).toBe("");
  });

  test("recordSignals writes moderation rows and skips the empty list", async () => {
    const { events, bodies } = capture();
    await events.recordSignals([]);
    expect(bodies).toHaveLength(0);
    await events.recordSignals([{ playerId: "k1", kind: "request-burst", detail: "many", count: 61 }]);
    expect(bodies[0]).toContain("INSERT INTO `moderation_signals`");
    expect(firstRow(bodies[0])).toEqual({ player_id: "k1", kind: "request-burst", detail: "many", count: 61 });
  });
});

describe("NullEvents", () => {
  test("accepts everything and is healthy", async () => {
    const events = new NullEvents();
    await events.recordAssessment({ requestId: "r", runtime: "python", passed: 0, total: 1 });
    await events.recordSignals([{ kind: "x" }]);
    expect(await events.ping()).toBe(true);
  });
});
