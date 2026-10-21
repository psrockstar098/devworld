// Integration tests: run against the live infra (ClickHouse, Kvrocks, Redis).
//
//   cd infra && ./scripts/up.sh
//   cd game-server && DEVWORLD_IT=1 bun test tests/integration.test.ts
//
// Skipped by default so `bun test` stays hermetic.
import { describe, expect, test, beforeAll, afterAll } from "bun:test";
import { loadConfig } from "../src/config";
import { ClickHouse } from "../src/clickhouse";
import { ClickHouseHistory } from "../src/history";
import { RedisKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";
import { createApp } from "../src/app";
import { loadMissionCatalog } from "../src/missions";

const enabled = process.env.DEVWORLD_IT === "1";
const d = enabled ? describe : describe.skip;
const run = Date.now().toString(36);
const playerId = `it-${run}`;

const cfg = loadConfig();

d("live stores", () => {
  const ch = new ClickHouse(cfg.clickhouse);
  const history = new ClickHouseHistory(ch);
  const kv = new RedisKv(cfg.kvUrl);
  const cache = new RedisKv(cfg.redisUrl);

  afterAll(async () => {
    await Promise.allSettled([kv.close(), cache.close()]);
  });

  test("all three stores answer", async () => {
    expect(await history.ping()).toBe(true);
    expect(await kv.ping()).toBe(true);
    expect(await cache.ping()).toBe(true);
  });

  test("writes an event and reads it back from ClickHouse", async () => {
    await history.insertEvents([{ playerId, event: "it.event", service: "integration" }]);
    const rows = await ch.query<{ n: string }>(
      "SELECT count() AS n FROM events WHERE player_id = {p:String} AND event = 'it.event'",
      { p: playerId },
    );
    expect(Number(rows[0].n)).toBeGreaterThanOrEqual(1);
  });

  test("the ledger view derives a balance", async () => {
    const ref = `it:${run}`;
    await history.insertLedger([
      { ref, playerId, account: "system", amount: -42 },
      { ref, playerId, account: "wallet", amount: 42 },
    ]);
    // SummingMergeTree views are read with an aggregate.
    await new Promise((r) => setTimeout(r, 250));
    expect(await history.playerBalance(playerId)).toBe(42);
  });

  test("durable KV survives in Kvrocks", async () => {
    await kv.set(`it:${run}`, "value", 30);
    expect(await kv.get(`it:${run}`)).toBe("value");
    expect(await kv.ttl(`it:${run}`)).toBeGreaterThan(0);
  });
});

d("full API flow against live stores", () => {
  const ch = new ClickHouse(cfg.clickhouse);
  const history = new ClickHouseHistory(ch);
  const kv = new RedisKv(cfg.kvUrl);
  const cache = new RedisKv(cfg.redisUrl);
  const assess = new FakeAssessClient((input) => ({
    passed: input.tests.length,
    total: input.tests.length,
    results: input.tests.map((t) => ({ name: t.name, passed: true, hidden: t.hidden })),
  }));
  const missions = loadMissionCatalog([
    {
      id: "it01",
      title: "Integration",
      language: "python",
      level: 1,
      requires: [],
      xpReward: 100,
      creditReward: 7,
      tests: [{ name: "t", stdin: "", expected: "ok", hidden: false }],
    },
  ]);
  const app = createApp({ history, kv, cache, assess, missions });
  const id = `it-api-${run}`;

  const call = (path: string, init?: RequestInit) => app.fetch(new Request("http://localhost" + path, init));

  afterAll(async () => {
    await Promise.allSettled([kv.close(), cache.close()]);
  });

  test("create → login → submit → balance, end to end", async () => {
    const created = await call("/players", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ id, handle: `it_${run}` }),
    });
    expect(created.status).toBe(201);

    // Claim the passwordless account, then log in with the password.
    const claimed = await call("/auth/register", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ id, handle: `it_${run}`, password: "correct horse battery" }),
    });
    expect(claimed.status).toBe(201);

    const login = await call("/auth/login", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ playerId: id, password: "correct horse battery" }),
    });
    const { token } = (await login.json()) as { token: string };

    const submit = await call("/missions/it01/submit", {
      method: "POST",
      headers: { "content-type": "application/json", authorization: `Bearer ${token}` },
      body: JSON.stringify({ files: [{ path: "main.py", content: "print('ok')" }] }),
    });
    expect(submit.status).toBe(200);
    const body = (await submit.json()) as { rewarded: boolean; balance: number; progression: { level: number } };
    expect(body.rewarded).toBe(true);
    expect(body.progression.level).toBe(2);
    expect(body.balance).toBe(7);
  });
});
