import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";

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
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ type: "company.founded", title: "Acme" });
  });

  test("list is newest-first and filters by type", async () => {
    const h = harness();
    const token = await h.signUp();
    await h.post("/events", { type: "company.founded", title: "First" }, { adminKey: "secret" });
    await h.post("/events", { type: "product.launched", title: "Second" }, { adminKey: "secret" });
    await h.post("/events", { type: "company.founded", title: "Third" }, { adminKey: "secret" });

    const all = (await (await h.get("/events", token)).json()) as { events: { title: string }[] };
    expect(all.events.map((e) => e.title)).toEqual(["Third", "Second", "First"]);

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
});
