import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";
import { loadMissionCatalog } from "../src/missions";

function harness(adminKey = "secret") {
  const history = new MemoryHistory();
  const kv = new MemoryKv();
  const cache = new MemoryKv();
  const assess = new FakeAssessClient(() => ({ passed: 0, total: 0, durationMs: 0, results: [] }));
  const app = createApp({ history, kv, cache, assess, missions: new Map(), adminKey });
  const call = (path: string, init?: RequestInit) => app.fetch(new Request("http://localhost" + path, init));
  const post = (path: string, body: unknown, opts: { token?: string; adminKey?: string } = {}) =>
    call(path, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        ...(opts.token ? { authorization: `Bearer ${opts.token}` } : {}),
        ...(opts.adminKey ? { "x-admin-key": opts.adminKey } : {}),
      },
      body: JSON.stringify(body),
    });
  const get = (path: string, token?: string) =>
    call(path, { headers: token ? { authorization: `Bearer ${token}` } : {} });

  async function signUp(id = "player-1", handle = "starter") {
    await post("/players", { id, handle });
    const res = await post("/auth/login", { playerId: id });
    const { token } = (await res.json()) as { token: string };
    return token;
  }

  return { post, get, signUp };
}

describe("world events", () => {
  test("record requires the admin key; list requires auth", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/events", { type: "company.founded", title: "Acme" })).status).toBe(401);
    expect((await h.post("/events", { type: "company.founded", title: "Acme" }, { adminKey: "wrong" })).status).toBe(401);
    expect((await h.get("/events")).status).toBe(401);
    const ok = await h.post("/events", { type: "company.founded", title: "Acme" }, { adminKey: "secret" });
    expect(ok.status).toBe(201);
    const listed = await h.get("/events", token);
    expect(listed.status).toBe(200);
    const { events } = (await listed.json()) as { events: { type: string; title: string }[] };
    // signUp itself writes a player.joined event, so the log holds both.
    expect(events.map((e) => e.type)).toEqual(["company.founded", "player.joined"]);
    expect(events[0]).toMatchObject({ type: "company.founded", title: "Acme" });
  });

  test("list is newest-first and filters by type", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/events", { type: "company.founded", title: "First" }, { adminKey: "secret" });
    await h.post("/events", { type: "product.launched", title: "Second" }, { adminKey: "secret" });
    await h.post("/events", { type: "company.founded", title: "Third" }, { adminKey: "secret" });

    const all = (await (await h.get("/events", token)).json()) as { events: { title: string }[] };
    // signUp writes a player.joined event first, so it sits at the end.
    expect(all.events.map((e) => e.title)).toEqual(["Third", "Second", "First", "starter joined the world"]);

    const filtered = (await (await h.get("/events?type=company.founded", token)).json()) as {
      events: { title: string }[];
    };
    expect(filtered.events.map((e) => e.title)).toEqual(["Third", "First"]);

    const limited = (await (await h.get("/events?limit=2", token)).json()) as { events: { title: string }[] };
    expect(limited.events.map((e) => e.title)).toEqual(["Third", "Second"]);
  });

  test("invalid event input is rejected", async () => {
    const h = harness();
    expect((await h.post("/events", { type: "bad type!", title: "X" }, { adminKey: "secret" })).status).toBe(400);
    expect((await h.post("/events", { type: "company.founded", title: "" }, { adminKey: "secret" })).status).toBe(400);
    expect((await h.post("/events", { type: "company.founded", title: "x".repeat(201) }, { adminKey: "secret" })).status).toBe(400);
  });

  test("no admin key configured means events cannot be recorded", async () => {
    const history = new MemoryHistory();
    const kv = new MemoryKv();
    const cache = new MemoryKv();
    const assess = new FakeAssessClient(() => ({ passed: 0, total: 0, durationMs: 0, results: [] }));
    const app = createApp({ history, kv, cache, assess, missions: new Map() });
    const res = await app.fetch(
      new Request("http://localhost/events", {
        method: "POST",
        headers: { "content-type": "application/json", "x-admin-key": "secret" },
        body: JSON.stringify({ type: "company.founded", title: "X" }),
      }),
    );
    expect(res.status).toBe(404);
  });

  test("playing the game writes world events: join + mission completion", async () => {
    const history = new MemoryHistory();
    const kv = new MemoryKv();
    const cache = new MemoryKv();
    const assess = new FakeAssessClient((input) => ({
      passed: input.tests.length,
      total: input.tests.length,
      durationMs: 7,
      results: input.tests.map((t) => ({ name: t.name, passed: true, hidden: false, actual: "Hello, Starter Town!" })),
    }));
    const missions = loadMissionCatalog({
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
          tests: [{ name: "greet", stdin: "", expected: "Hello, Starter Town!", hidden: false }],
        },
      ],
    });
    const app = createApp({ history, kv, cache, assess, missions });
    const call = (path: string, init?: RequestInit) => app.fetch(new Request("http://localhost" + path, init));
    const post = (path: string, body: unknown, token?: string) =>
      call(path, {
        method: "POST",
        headers: { "content-type": "application/json", ...(token ? { authorization: `Bearer ${token}` } : {}) },
        body: JSON.stringify(body),
      });
    const get = (path: string, token?: string) =>
      call(path, { headers: token ? { authorization: `Bearer ${token}` } : {} });

    await post("/players", { id: "player-1", handle: "starter" });
    const login = await post("/auth/login", { playerId: "player-1" });
    const { token } = (await login.json()) as { token: string };
    const submit = await post("/missions/m01/submit", { files: [{ path: "main.py", content: "print('Hello')" }] }, token);
    expect(submit.status).toBe(200);
    expect(((await submit.json()) as { rewarded: boolean }).rewarded).toBe(true);

    const { events } = (await (await get("/events", token)).json()) as {
      events: { type: string; title: string }[];
    };
    expect(events.map((e) => e.type)).toEqual(["mission.completed", "player.joined"]);
    expect(events[0].title).toContain("First Boot");
    expect(events[1].title).toContain("starter");
  });
});
