import { describe, expect, test } from "bun:test";
import { createApp, hashSubmission, validateFiles, type AppDeps } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient, type AssessInput } from "../src/assess";
import { loadMissionCatalog } from "../src/missions";

const CATALOG = loadMissionCatalog({
  missions: [
    {
      id: "m01",
      title: "First Boot",
      language: "python",
      level: 1,
      requires: [],
      xpReward: 150,
      creditReward: 10,
      gate: false,
      tests: [
        { name: "greet", stdin: "", expected: "Hello, Starter Town!", hidden: false },
        { name: "edge", stdin: "", expected: "HIDDEN-EXPECTED", hidden: true },
      ],
    },
    {
      id: "m02",
      title: "Second Step",
      language: "python",
      level: 2,
      requires: ["m01"],
      xpReward: 200,
      creditReward: 20,
      gate: false,
      tests: [{ name: "p1", stdin: "", expected: "ok", hidden: false }],
    },
    {
      id: "m05",
      title: "Locked High",
      language: "python",
      level: 5,
      requires: [],
      xpReward: 0,
      creditReward: 0,
      gate: false,
      tests: [{ name: "p1", stdin: "", expected: "ok", hidden: false }],
    },
  ],
});

function harness(over: Partial<AppDeps> = {}) {
  const history = new MemoryHistory();
  const kv = new MemoryKv();
  const cache = new MemoryKv();
  const assess = new FakeAssessClient((input: AssessInput) => {
    const fails = input.files.some((f) => f.content.includes("FAIL"));
    return {
      passed: fails ? input.tests.length - 1 : input.tests.length,
      total: input.tests.length,
      durationMs: 7,
      results: input.tests.map((t) => ({
        name: t.name,
        passed: fails ? !t.hidden : true,
        hidden: t.hidden,
        actual: t.hidden ? undefined : "Hello, Starter Town!",
      })),
    };
  });
  const app = createApp({ history, kv, cache, assess, missions: CATALOG, adminKey: "secret", ...over });
  const call = (path: string, init?: RequestInit) =>
    app.fetch(new Request("http://localhost" + path, init));
  const post = (path: string, body: unknown, token?: string) =>
    call(path, {
      method: "POST",
      headers: { "content-type": "application/json", ...(token ? { authorization: `Bearer ${token}` } : {}) },
      body: JSON.stringify(body),
    });
  const get = (path: string, token?: string) =>
    call(path, { headers: token ? { authorization: `Bearer ${token}` } : {} });

  async function signUp(id = "player-1", handle = "starter") {
    await post("/auth/register", { id, handle, password: "correct horse battery" });
    const res = await post("/auth/login", { playerId: id, password: "correct horse battery" });
    const { token } = (await res.json()) as { token: string };
    return token;
  }

  return { app, call, post, get, signUp, history, kv, cache, assess };
}

const files = (content = "print('Hello, Starter Town!')") => [{ path: "main.py", content }];

describe("GET /health", () => {
  test("reports each store and is 200 when all are up", async () => {
    const res = await harness().get("/health");
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({
      ok: true,
      stores: { clickhouse: true, kvrocks: true, redis: true },
      missions: 3,
    });
  });

  test("is 503 when a store is down", async () => {
    const h = harness();
    h.history.healthy = false;
    const res = await h.get("/health");
    expect(res.status).toBe(503);
    expect(((await res.json()) as { ok: boolean }).ok).toBe(false);
  });
});

describe("players and sessions", () => {
  test("create → login → me", async () => {
    const h = harness();
    const created = await h.post("/players", { id: "player-1", handle: "starter" });
    expect(created.status).toBe(201);

    const token = await h.signUp("player-2", "other");
    const me = await h.get("/me", token);
    expect(me.status).toBe(200);
    const body = (await me.json()) as Record<string, unknown>;
    expect(body.id).toBe("player-2");
    expect(body.balance).toBe(0);
    expect((body.progression as { level: number }).level).toBe(1);
  });

  test("rejects a duplicate player and a taken handle", async () => {
    const h = harness();
    await h.post("/players", { id: "player-1", handle: "starter" });
    expect((await h.post("/players", { id: "player-1", handle: "again" })).status).toBe(409);
    expect((await h.post("/players", { id: "player-9", handle: "starter" })).status).toBe(409);
  });

  test("rejects bad ids and handles", async () => {
    const h = harness();
    expect((await h.post("/players", { id: "x", handle: "starter" })).status).toBe(400);
    expect((await h.post("/players", { id: "player-1", handle: "NOPE" })).status).toBe(400);
  });

  test("login for an unknown player is 404; /me without a token is 401", async () => {
    const h = harness();
    expect((await h.post("/auth/login", { playerId: "player-1" })).status).toBe(404);
    expect((await h.get("/me")).status).toBe(401);
  });

  test("logout invalidates the token", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/auth/logout", {}, token)).status).toBe(200);
    expect((await h.get("/me", token)).status).toBe(401);
  });

  test("GET /players/:id returns the public subset only", async () => {
    const h = harness();
    await h.post("/players", { id: "player-1", handle: "starter" });
    const res = await h.get("/players/player-1");
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, unknown>;
    expect(Object.keys(body).sort()).toEqual(["completedMissions", "displayName", "handle", "id"]);
    expect((await h.get("/players/nobody00")).status).toBe(404);
  });
});

describe("GET /missions", () => {
  test("never leaks hidden test data", async () => {
    const res = await harness().get("/missions");
    const text = await res.text();
    expect(text).not.toContain("HIDDEN-EXPECTED");
    const body = JSON.parse(text) as { missions: Array<{ id: string; tests: Array<Record<string, unknown>> }> };
    const m01 = body.missions.find((m) => m.id === "m01")!;
    const hidden = m01.tests.find((t) => t.hidden)!;
    expect(hidden).toEqual({ name: "edge", hidden: true });
    const pub = m01.tests.find((t) => !t.hidden)!;
    expect(pub.expected).toBe("Hello, Starter Town!");
  });
});

describe("POST /missions/:id/submit", () => {
  test("a passing submission grants xp + credits exactly once", async () => {
    const h = harness();
    const token = await h.signUp();
    const res = await h.post("/missions/m01/submit", { files: files() }, token);
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, any>;
    expect(body.passed).toBe(true);
    expect(body.publicPassed).toBe(1);
    expect(body.hiddenPassed).toBe(1);
    expect(body.rewarded).toBe(true);
    expect(body.progression.totalXp).toBe(150);
    expect(body.progression.level).toBe(2);
    expect(body.balance).toBe(10);

    // Ledger is balanced and the player record is updated.
    const net = h.history.ledger.filter((r) => r.playerId === "player-1").reduce((a, r) => a + r.amount, 0);
    expect(net).toBe(0);
    expect((await h.kv.get("player:player-1"))!).toContain("m01");
    expect(h.history.attempts).toHaveLength(1);
    expect(h.history.attempts[0].codeHash).toBe(hashSubmission(files()));
  });

  test("resubmitting a completed mission is refused", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/missions/m01/submit", { files: files() }, token);
    const again = await h.post("/missions/m01/submit", { files: files() }, token);
    expect(again.status).toBe(409);
  });

  test("a hidden failure withholds rewards but still records the attempt", async () => {
    const h = harness();
    const token = await h.signUp();
    const res = await h.post("/missions/m01/submit", { files: files("FAIL") }, token);
    const body = (await res.json()) as Record<string, any>;
    expect(body.passed).toBe(false);
    expect(body.rewarded).toBe(false);
    expect(body.hiddenPassed).toBe(0);
    expect(body.progression.totalXp).toBe(0);
    expect(h.history.attempts[0].passed).toBe(false);
    expect(await h.kv.get("player:player-1")).not.toContain("m01");
  });

  test("enforces mission prerequisites and level gates", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/missions/m02/submit", { files: files() }, token)).status).toBe(403);
    expect((await h.post("/missions/m05/submit", { files: files() }, token)).status).toBe(403);
  });

  test("prerequisites open once the requirement is complete", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/missions/m01/submit", { files: files() }, token);
    const res = await h.post("/missions/m02/submit", { files: files("print('ok')") }, token);
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, any>;
    expect(body.progression.totalXp).toBe(350); // 150 + 200
  });

  test("unknown mission, missing auth, bad files", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/missions/nope/submit", { files: files() }, token)).status).toBe(404);
    expect((await h.post("/missions/m01/submit", { files: files() })).status).toBe(401);
    expect((await h.post("/missions/m01/submit", { files: [] }, token)).status).toBe(400);
    expect((await h.post("/missions/m01/submit", { files: [{ path: "../x", content: "hi" }] }, token)).status).toBe(400);
  });

  test("returns 503 when no grading backend is configured", async () => {
    const h = harness({ assess: null });
    const token = await h.signUp();
    const res = await h.post("/missions/m01/submit", { files: files() }, token);
    expect(res.status).toBe(503);
  });
});

describe("ledger", () => {
  test("balance is cached after a reward and invalidated by an admin transaction", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/missions/m01/submit", { files: files() }, token);
    expect(((await (await h.get("/ledger/balance", token)).json()) as any).balance).toBe(10);

    const admin = await h.call("/admin/ledger", {
      method: "POST",
      headers: { "content-type": "application/json", "x-admin-key": "secret" },
      body: JSON.stringify({
        entries: [
          { ref: "bonus-1", playerId: "player-1", account: "system", amount: -5 },
          { ref: "bonus-1", playerId: "player-1", account: "wallet", amount: 5 },
        ],
      }),
    });
    expect(admin.status).toBe(200);
    expect(((await (await h.get("/ledger/balance", token)).json()) as any).balance).toBe(15);
  });

  test("admin endpoint rejects bad keys and unbalanced transactions", async () => {
    const h = harness();
    expect(
      (
        await h.call("/admin/ledger", {
          method: "POST",
          headers: { "content-type": "application/json", "x-admin-key": "wrong" },
          body: JSON.stringify({ entries: [] }),
        })
      ).status,
    ).toBe(401);
    expect(
      (
        await h.call("/admin/ledger", {
          method: "POST",
          headers: { "content-type": "application/json", "x-admin-key": "secret" },
          body: JSON.stringify({
            entries: [
              { ref: "r", playerId: "p", account: "a", amount: 10 },
              { ref: "r", playerId: "p", account: "b", amount: -9 },
            ],
          }),
        })
      ).status,
    ).toBe(400);
  });

  test("is hidden when no admin key is configured", async () => {
    const h = harness({ adminKey: undefined });
    const res = await h.post("/admin/ledger", { entries: [] });
    expect(res.status).toBe(404);
  });
});

describe("GET /stats/missions", () => {
  test("aggregates attempts and passes", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/missions/m01/submit", { files: files("FAIL") }, token);
    const res = await h.get("/stats/missions");
    expect(await res.json()).toEqual({ missions: [{ missionId: "m01", attempts: 1, passes: 0 }] });
  });
});

describe("validateFiles", () => {
  test("accepts nested relative paths", () => {
    expect(validateFiles([{ path: "src/main.py", content: "x" }])).toHaveLength(1);
  });

  test("rejects traversal, absolute paths, duplicates and oversize", () => {
    expect(() => validateFiles([{ path: "/etc/passwd", content: "x" }])).toThrow();
    expect(() => validateFiles([{ path: "a/../b", content: "x" }])).toThrow();
    expect(() =>
      validateFiles([
        { path: "a.py", content: "x" },
        { path: "a.py", content: "y" },
      ]),
    ).toThrow(/duplicate/);
    expect(() => validateFiles([{ path: "big.py", content: "x".repeat(200 * 1024 + 1) }])).toThrow(/too large/);
  });
});

describe("routing", () => {
  test("unknown routes are 404 and malformed JSON is 400", async () => {
    const h = harness();
    expect((await h.get("/nope")).status).toBe(404);
    const bad = await h.call("/players", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: "{not json",
    });
    expect(bad.status).toBe(400);
  });

  test("hashSubmission is stable and order independent", () => {
    const a = hashSubmission([
      { path: "b.py", content: "2" },
      { path: "a.py", content: "1" },
    ]);
    const b = hashSubmission([
      { path: "a.py", content: "1" },
      { path: "b.py", content: "2" },
    ]);
    expect(a).toBe(b);
    expect(a).toHaveLength(16);
    expect(a).not.toBe(hashSubmission([{ path: "a.py", content: "3" }]));
  });
});
