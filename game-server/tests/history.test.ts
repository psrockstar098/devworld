import { describe, expect, test } from "bun:test";
import { ClickHouse, type FetchLike } from "../src/clickhouse";
import { ClickHouseHistory, MemoryHistory } from "../src/history";

/** Capture the JSONEachRow body of each insert. */
function capture() {
  const bodies: string[] = [];
  const fetchImpl: FetchLike = async (_url, init) => {
    if (init?.body) bodies.push(init.body);
    return new Response("", { status: 200 });
  };
  const ch = new ClickHouse(
    { url: "http://ch:8123", user: "u", password: "p", database: "devworld" },
    fetchImpl,
  );
  return { history: new ClickHouseHistory(ch), bodies };
}

/** Parse the first JSON object after the FORMAT line of an INSERT body. */
function firstRow(body: string): Record<string, unknown> {
  const line = body.split("\n").find((l) => l.startsWith("{"));
  return JSON.parse(line ?? "{}");
}

describe("ClickHouseHistory column mapping", () => {
  test("xp rows use snake_case columns (player_id, level_after)", async () => {
    const { history, bodies } = capture();
    await history.insertXp([{ playerId: "p1", source: "mission", amount: 100, levelAfter: 2, ref: "m01" }]);
    const row = firstRow(bodies[0]);
    expect(row).toEqual({ player_id: "p1", source: "mission", amount: 100, level_after: 2, ref: "m01" });
  });

  test("ledger rows use snake_case and default the currency", async () => {
    const { history, bodies } = capture();
    await history.insertLedger([{ ref: "r", playerId: "p1", account: "wallet", amount: 5 }]);
    expect(firstRow(bodies[0])).toEqual({
      ref: "r",
      player_id: "p1",
      account: "wallet",
      amount: 5,
      currency: "credits",
    });
  });

  test("mission attempts serialize passed as 0/1 and fill defaults", async () => {
    const { history, bodies } = capture();
    await history.insertMissionAttempt({ playerId: "p1", missionId: "m01", runtime: "python", passed: true });
    const row = firstRow(bodies[0]);
    expect(row.player_id).toBe("p1");
    expect(row.mission_id).toBe("m01");
    expect(row.passed).toBe(1);
    expect(row.public_passed).toBe(0);
    expect(row.hidden_passed).toBe(0);
    expect(row.code_hash).toBe("");
    expect(row.duration_ms).toBe(0);
  });

  test("assessment rows use request_id / timed_out / error_kind", async () => {
    const { history, bodies } = capture();
    await history.insertAssessment({ requestId: "req1", runtime: "python", passed: 3, total: 3, timedOut: true, errorKind: "timeout" });
    expect(firstRow(bodies[0])).toEqual({
      request_id: "req1",
      runtime: "python",
      passed: 3,
      total: 3,
      duration_ms: 0,
      timed_out: 1,
      error_kind: "timeout",
    });
  });

  test("events stringify props and map session_id", async () => {
    const { history, bodies } = capture();
    await history.insertEvents([{ playerId: "p1", sessionId: "s1", event: "player.created", service: "game-server", props: { a: 1 } }]);
    const row = firstRow(bodies[0]);
    expect(row.player_id).toBe("p1");
    expect(row.session_id).toBe("s1");
    expect(row.event).toBe("player.created");
    expect(row.props).toBe('{"a":1}');
  });

  test("undefined values are omitted so ClickHouse defaults apply", async () => {
    const { history, bodies } = capture();
    await history.insertXp([{ playerId: "p1", source: "mission", amount: 1 }]);
    expect(bodies[0]).not.toContain("level_after");
    expect(bodies[0]).not.toContain('"ref"');
  });

  test("the aggregate reads filter to the wallet account", async () => {
    const urls: string[] = [];
    const bodies: string[] = [];
    const fetchImpl: FetchLike = async (url, init) => {
      urls.push(url);
      if (init?.body) bodies.push(init.body);
      return new Response('{"b":"100"}\n', { status: 200 });
    };
    const history = new ClickHouseHistory(
      new ClickHouse({ url: "http://ch:8123", user: "u", password: "p", database: "devworld" }, fetchImpl),
    );
    expect(await history.playerBalance("p1")).toBe(100);
    expect(decodeURIComponent(bodies[0])).toContain("account = 'wallet'");
  });
});

describe("MemoryHistory mirrors the views", () => {
  test("balance counts only wallet rows; xp and stats aggregate", async () => {
    const h = new MemoryHistory();
    await h.insertLedger([
      { ref: "r", playerId: "p", account: "system", amount: -10 },
      { ref: "r", playerId: "p", account: "wallet", amount: 10 },
    ]);
    expect(await h.playerBalance("p")).toBe(10);
    await h.insertXp([
      { playerId: "p", source: "mission", amount: 100 },
      { playerId: "p", source: "mission", amount: 50 },
    ]);
    expect(await h.playerTotalXp("p")).toBe(150);
    await h.insertMissionAttempt({ playerId: "p", missionId: "m01", runtime: "python", passed: true });
    await h.insertMissionAttempt({ playerId: "p", missionId: "m01", runtime: "python", passed: false });
    expect(await h.missionStats()).toEqual([{ missionId: "m01", attempts: 2, passes: 1 }]);
  });
});
