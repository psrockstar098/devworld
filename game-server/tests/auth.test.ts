import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { MemoryHistory } from "../src/history";
import { MemoryKv } from "../src/kv";
import { FakeAssessClient } from "../src/assess";

const PW = "correct horse battery";

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
  return { post, get };
}

describe("credential auth", () => {
  test("register → login works; the old id-only login is closed", async () => {
    const h = harness();
    const reg = await h.post("/auth/register", { id: "player-1", handle: "starter", password: PW });
    expect(reg.status).toBe(201);
    const login = await h.post("/auth/login", { playerId: "player-1", password: PW });
    expect(login.status).toBe(200);
    const { token } = (await login.json()) as { token: string };
    expect(token.length).toBeGreaterThan(20);
    expect((await h.get("/me", token)).status).toBe(200);

    // The vulnerability: id alone must never yield a token, even for a
    // real registered player.
    expect((await h.post("/auth/login", { playerId: "player-1" })).status).toBe(400);
    expect((await h.post("/auth/login", { playerId: "player-1", password: "wrong password here" })).status).toBe(401);
    expect((await h.post("/auth/login", { playerId: "ghost-99", password: PW })).status).toBe(404);
  });

  test("register validation: short password, duplicate, bad id/handle", async () => {
    const h = harness();
    expect((await h.post("/auth/register", { id: "player-1", handle: "starter", password: "short" })).status).toBe(400);
    expect((await h.post("/auth/register", { id: "player-1", handle: "starter" })).status).toBe(400);
    expect((await h.post("/auth/register", { id: "x", handle: "starter", password: PW })).status).toBe(400);
    expect((await h.post("/auth/register", { id: "player-1", handle: "NOPE", password: PW })).status).toBe(400);
    expect((await h.post("/auth/register", { id: "player-1", handle: "starter", password: PW })).status).toBe(201);
    expect((await h.post("/auth/register", { id: "player-1", handle: "starter", password: PW })).status).toBe(409);
  });

  test("register claims a passwordless /players account", async () => {
    const h = harness();
    expect((await h.post("/players", { id: "player-1", handle: "starter" })).status).toBe(201);
    expect((await h.post("/auth/login", { playerId: "player-1", password: PW })).status).toBe(401);
    expect((await h.post("/auth/register", { id: "player-1", handle: "starter", password: PW })).status).toBe(201);
    expect((await h.post("/auth/login", { playerId: "player-1", password: PW })).status).toBe(200);
  });

  test("logout still revokes the token", async () => {
    const h = harness();
    await h.post("/auth/register", { id: "player-1", handle: "starter", password: PW });
    const login = await h.post("/auth/login", { playerId: "player-1", password: PW });
    const { token } = (await login.json()) as { token: string };
    expect((await h.post("/auth/logout", {}, token)).status).toBe(200);
    expect((await h.get("/me", token)).status).toBe(401);
  });
});
