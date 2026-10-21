import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";

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

  return { post, get, signUp };
}

describe("friends", () => {
  test("request validation: auth, self, unknown player, duplicates", async () => {
    const h = harness();
    const token = await h.signUp();
    expect((await h.post("/friends/request", { playerId: "player-2" })).status).toBe(401);
    expect((await h.post("/friends/request", { playerId: "player-1" }, token)).status).toBe(400);
    expect((await h.post("/friends/request", { playerId: "ghost-99" }, token)).status).toBe(404);
    expect((await h.post("/friends/request", { playerId: "bad!" }, token)).status).toBe(400);

    const token2 = await h.signUp("player-2", "other");
    expect((await h.post("/friends/request", { playerId: "player-2" }, token)).status).toBe(200);
    expect((await h.post("/friends/request", { playerId: "player-2" }, token)).status).toBe(409);
    expect((await h.post("/friends/accept", { playerId: "player-1" }, token2)).status).toBe(200);
    expect((await h.post("/friends/request", { playerId: "player-2" }, token)).status).toBe(409);
  });

  test("request → accept makes both sides friends; requests list clears", async () => {
    const h = harness();
    const token = await h.signUp();
    const token2 = await h.signUp("player-2", "other");
    await h.post("/friends/request", { playerId: "player-2" }, token);

    const reqs = (await (await h.get("/friends/requests", token2)).json()) as {
      requests: { playerId: string; handle: string; displayName: string }[];
    };
    expect(reqs.requests).toEqual([{ playerId: "player-1", handle: "starter", displayName: "starter" }]);

    expect((await h.post("/friends/accept", { playerId: "player-1" }, token2)).status).toBe(200);
    const reqsAfter = (await (await h.get("/friends/requests", token2)).json()) as { requests: unknown[] };
    expect(reqsAfter.requests).toEqual([]);

    const f1 = (await (await h.get("/friends", token)).json()) as { friends: { playerId: string }[] };
    const f2 = (await (await h.get("/friends", token2)).json()) as { friends: { playerId: string }[] };
    expect(f1.friends.map((f) => f.playerId)).toEqual(["player-2"]);
    expect(f2.friends.map((f) => f.playerId)).toEqual(["player-1"]);
  });

  test("decline removes the request; accept afterwards fails", async () => {
    const h = harness();
    const token = await h.signUp();
    const token2 = await h.signUp("player-2", "other");
    await h.post("/friends/request", { playerId: "player-2" }, token);
    expect((await h.post("/friends/decline", { playerId: "player-1" }, token2)).status).toBe(200);
    expect((await h.post("/friends/accept", { playerId: "player-1" }, token2)).status).toBe(404);
    expect((await h.post("/friends/decline", { playerId: "player-1" }, token2)).status).toBe(404);
  });

  test("friends list shows online status and city from presence", async () => {
    const h = harness();
    const token = await h.signUp();
    const token2 = await h.signUp("player-2", "other");
    const token3 = await h.signUp("player-3", "third");
    await h.post("/friends/request", { playerId: "player-2" }, token);
    await h.post("/friends/accept", { playerId: "player-1" }, token2);
    await h.post("/friends/request", { playerId: "player-3" }, token);
    await h.post("/friends/accept", { playerId: "player-1" }, token3);
    await h.post("/presence/heartbeat", { city: "starter_town", map: "town" }, token2);

    const { friends } = (await (await h.get("/friends", token)).json()) as {
      friends: { playerId: string; online: boolean; city: string | null }[];
    };
    expect(friends.find((f) => f.playerId === "player-2")).toMatchObject({ online: true, city: "starter_town" });
    expect(friends.find((f) => f.playerId === "player-3")).toMatchObject({ online: false, city: null });
  });
});
