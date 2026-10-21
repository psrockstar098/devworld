import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";
import { PRESENCE_STALE_MS } from "../src/presence";

function harness() {
  const history = new MemoryHistory();
  const kv = new MemoryKv();
  const cache = new MemoryKv();
  const assess = new FakeAssessClient(() => ({ passed: 0, total: 0, durationMs: 0, results: [] }));
  const app = createApp({ history, kv, cache, assess, missions: new Map() });
  const call = (path: string, init?: RequestInit) => app.fetch(new Request("http://localhost" + path, init));
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

  return { post, get, signUp, cache };
}

describe("presence", () => {
  test("heartbeat and online require auth", async () => {
    const h = harness();
    expect((await h.post("/presence/heartbeat", { city: "starter_town" })).status).toBe(401);
    expect((await h.get("/presence/online")).status).toBe(401);
  });

  test("heartbeat shows the player online with their location", async () => {
    const h = harness();
    const token = await h.signUp();
    const hb = await h.post("/presence/heartbeat", { city: "starter_town", map: "town" }, token);
    expect(hb.status).toBe(200);
    const body = (await hb.json()) as { ok: boolean; playerId: string; city: string; map: string };
    expect(body.ok).toBe(true);
    expect(body.playerId).toBe("player-1");
    expect(body.city).toBe("starter_town");

    const online = await h.get("/presence/online", token);
    expect(online.status).toBe(200);
    const { players } = (await online.json()) as { players: { playerId: string; handle: string; city: string; map: string }[] };
    expect(players).toHaveLength(1);
    expect(players[0]).toMatchObject({ playerId: "player-1", handle: "starter", city: "starter_town", map: "town" });
  });

  test("a second heartbeat moves the player; one entry per player", async () => {
    const h = harness();
    const token = await h.signUp();
    const token2 = await h.signUp("player-2", "other");
    await h.post("/presence/heartbeat", { city: "starter_town" }, token);
    await h.post("/presence/heartbeat", { city: "startup_city" }, token2);
    await h.post("/presence/heartbeat", { city: "systems_district", map: "systems_datacenter" }, token);

    const { players } = (await (await h.get("/presence/online", token)).json()) as {
      players: { playerId: string; city: string; map: string }[];
    };
    expect(players).toHaveLength(2);
    expect(players.find((p) => p.playerId === "player-1")).toMatchObject({ city: "systems_district", map: "systems_datacenter" });
    expect(players.find((p) => p.playerId === "player-2")).toMatchObject({ city: "startup_city" });
  });

  test("invalid city or map is rejected", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/presence/heartbeat", { city: "" }, token)).status).toBe(400);
    expect((await h.post("/presence/heartbeat", { city: "bad city!" }, token)).status).toBe(400);
    expect((await h.post("/presence/heartbeat", { city: "starter_town", map: "a/b" }, token)).status).toBe(400);
  });

  test("stale entries are pruned from the online list", async () => {
    const h = harness();
    const token = await h.signUp();
    const now = Date.now();
    await h.cache.set(
      "presence:online",
      JSON.stringify({
        "ghost-1": { playerId: "ghost-1", city: "starter_town", map: "", lastSeenMs: now - PRESENCE_STALE_MS - 10_000 },
        "player-1": { playerId: "player-1", city: "starter_town", map: "town", lastSeenMs: now },
      }),
    );
    const { players } = (await (await h.get("/presence/online", token)).json()) as {
      players: { playerId: string }[];
    };
    expect(players.map((p) => p.playerId)).toEqual(["player-1"]);
  });
});
