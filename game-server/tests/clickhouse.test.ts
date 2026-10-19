import { describe, expect, test } from "bun:test";
import { ClickHouse, ClickHouseError, quoteTable, type FetchLike } from "../src/clickhouse";

interface Call {
  url: string;
  body?: string;
  headers?: Record<string, string>;
}

function stub(response: { status?: number; body?: string } = {}) {
  const calls: Call[] = [];
  const fetchImpl: FetchLike = async (url, init) => {
    calls.push({ url, body: init?.body, headers: init?.headers });
    return new Response(response.body ?? "", { status: response.status ?? 200 });
  };
  return { calls, fetchImpl };
}

const ch = (fetchImpl: FetchLike) =>
  new ClickHouse(
    { url: "http://ch:8123", user: "devworld", password: "pw", database: "devworld" },
    fetchImpl,
  );

describe("quoteTable", () => {
  test("quotes simple and qualified names", () => {
    expect(quoteTable("events")).toBe("`events`");
    expect(quoteTable("devworld.events")).toBe("`devworld`.`events`");
  });

  test("rejects injection attempts", () => {
    expect(() => quoteTable("events; DROP TABLE x")).toThrow(ClickHouseError);
    expect(() => quoteTable("a.b.c")).toThrow(ClickHouseError);
    expect(() => quoteTable("")).toThrow(ClickHouseError);
  });
});

describe("ClickHouse.query", () => {
  test("appends JSONEachRow and parses rows", async () => {
    const { calls, fetchImpl } = stub({ body: '{"n":1}\n{"n":2}\n' });
    const rows = await ch(fetchImpl).query<{ n: number }>("SELECT n FROM t");
    expect(rows).toEqual([{ n: 1 }, { n: 2 }]);
    expect(calls[0].body).toBe("SELECT n FROM t\nFORMAT JSONEachRow");
  });

  test("binds values as param_x and sets the database", async () => {
    const { calls, fetchImpl } = stub({ body: "" });
    await ch(fetchImpl).query("SELECT 1 WHERE player_id = {p:String}", { p: "player-1" });
    const u = new URL(calls[0].url);
    expect(u.searchParams.get("param_p")).toBe("player-1");
    expect(u.searchParams.get("database")).toBe("devworld");
    expect(calls[0].headers?.["X-ClickHouse-User"]).toBe("devworld");
  });

  test("strips a trailing semicolon before FORMAT", async () => {
    const { calls, fetchImpl } = stub({ body: "" });
    await ch(fetchImpl).query("SELECT 1;");
    expect(calls[0].body).toBe("SELECT 1\nFORMAT JSONEachRow");
  });

  test("throws ClickHouseError with the server message", async () => {
    const { fetchImpl } = stub({ status: 404, body: "Table x does not exist" });
    await expect(ch(fetchImpl).query("SELECT * FROM x")).rejects.toThrow(/does not exist/);
  });
});

describe("ClickHouse.insert", () => {
  test("builds a JSONEachRow INSERT", async () => {
    const { calls, fetchImpl } = stub({ body: "" });
    const n = await ch(fetchImpl).insert("events", [{ player_id: "p", event: "x" }]);
    expect(n).toBe(1);
    expect(calls[0].body).toBe('INSERT INTO `events` FORMAT JSONEachRow\n{"player_id":"p","event":"x"}');
  });

  test("skips empty inserts", async () => {
    const { calls, fetchImpl } = stub({ body: "" });
    expect(await ch(fetchImpl).insert("events", [])).toBe(0);
    expect(calls.length).toBe(0);
  });
});

describe("ClickHouse.ping", () => {
  test("true when the server answers, false when it errors", async () => {
    expect(await ch(stub({ body: "1" }).fetchImpl).ping()).toBe(true);
    expect(await ch(stub({ status: 500, body: "boom" }).fetchImpl).ping()).toBe(false);
  });
});
