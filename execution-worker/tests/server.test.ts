import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { loadRuntimes, getRuntime } from "../src/runtimes";
import { echoRunner } from "../src/runner";
import { Semaphore } from "../src/semaphore";
import { TokenBucket } from "../src/quota";

const runtimes = loadRuntimes(new URL("../runtimes.json", import.meta.url));
const headers = { "content-type": "application/json", "x-api-key": "test-key" };

function makeApp(overrides: Record<string, unknown> = {}) {
  return createApp({
    apiKey: "test-key",
    runtimes,
    runner: echoRunner(),
    quotaPerHour: 1000,
    maxConcurrent: 4,
    maxRequestBytes: 1024 * 1024,
    ...overrides,
  } as Parameters<typeof createApp>[0]);
}

describe("server routes", () => {
  test("GET /health needs no auth", async () => {
    const app = makeApp();
    const res = await app.fetch(new Request("http://localhost/health"));
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ ok: true });
  });

  test("GET /runtimes returns the registry", async () => {
    const app = makeApp();
    const res = await app.fetch(new Request("http://localhost/runtimes", { headers }));
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(Array.isArray(body.runtimes)).toBe(true);
    expect(body.runtimes.length).toBeGreaterThanOrEqual(18);
    const ids = body.runtimes.map((r: { id: string }) => r.id);
    for (const expected of [
      "python", "javascript", "typescript", "c", "cpp", "java", "csharp",
      "go", "rust", "sql", "swift", "kotlin", "dart", "julia", "scala",
      "haskell", "zig", "asm",
    ]) {
      expect(ids).toContain(expected);
    }
    // Spot-check required fields on every runtime.
    for (const r of body.runtimes) {
      for (const f of ["id", "label", "version", "image", "runCommand", "timeoutSec", "memory", "cpus", "network"]) {
        expect(r[f], `${r.id}.${f}`).toBeDefined();
      }
    }
  });

  test("bearer token also authenticates", async () => {
    const app = makeApp();
    const res = await app.fetch(
      new Request("http://localhost/runtimes", {
        headers: { authorization: "Bearer test-key" },
      }),
    );
    expect(res.status).toBe(200);
  });

  test("429 carries a Retry-After header", async () => {
    const app = makeApp({ quotaPerHour: 1 });
    const body = JSON.stringify({ runtime: "python", files: [{ path: "m.py", content: "x" }] });
    const mk = () =>
      app.fetch(new Request("http://localhost/execute", { method: "POST", headers, body }));
    expect((await mk()).status).toBe(200);
    const limited = await mk();
    expect(limited.status).toBe(429);
    expect(Number(limited.headers.get("retry-after"))).toBeGreaterThan(0);
  });
});

describe("TokenBucket", () => {
  test("refills over time", () => {
    const bucket = new TokenBucket(2, 3600); // 2 per hour = 1 per 30 min... use fast refill
    const fast = new TokenBucket(1, 3_600_000); // 1 token, refills 1000/sec
    expect(fast.take("k").allowed).toBe(true);
    expect(fast.take("k").allowed).toBe(false);
    expect(bucket.take("k").allowed).toBe(true);
    expect(bucket.take("k").allowed).toBe(true);
    expect(bucket.take("k").allowed).toBe(false);
    expect(bucket.take("other-key").allowed).toBe(true); // per-key isolation
  });

  test("rejects invalid config", () => {
    expect(() => new TokenBucket(0, 1)).toThrow();
    expect(() => new TokenBucket(1, 0)).toThrow();
  });
});

describe("Semaphore", () => {
  test("bounds concurrency and releases", async () => {
    const sem = new Semaphore(2);
    expect(await sem.acquire(10)).toBe(true);
    expect(await sem.acquire(10)).toBe(true);
    expect(await sem.acquire(10)).toBe(false); // exhausted, times out
    sem.release();
    expect(await sem.acquire(10)).toBe(true);
    sem.release();
    sem.release();
    expect(sem.available()).toBe(2);
  });

  test("waiters are served FIFO on release", async () => {
    const sem = new Semaphore(1);
    await sem.acquire(10);
    const order: string[] = [];
    const a = sem.acquire(1000).then((ok) => ok && order.push("a"));
    const b = sem.acquire(1000).then((ok) => ok && order.push("b"));
    sem.release();
    await a;
    sem.release();
    await b;
    expect(order).toEqual(["a", "b"]);
  });
});

describe("registry sanity", () => {
  test("custom images are marked with a build dir", () => {
    for (const id of ["typescript", "csharp", "sql", "kotlin", "scala", "zig", "asm"]) {
      const r = getRuntime(id)!;
      expect(r.imageBuild, id).toMatch(/^images\//);
      expect(r.image.startsWith("devworld/runtime-")).toBe(true);
    }
  });

  test("official images use upstream tags", () => {
    expect(getRuntime("python")!.image).toBe("python:3.14-slim");
    expect(getRuntime("javascript")!.image).toBe("node:22-slim");
    expect(getRuntime("go")!.image).toBe("golang:1.24-alpine");
    expect(getRuntime("rust")!.image).toBe("rust:1.84-slim");
    expect(getRuntime("swift")!.image).toBe("swift:6.1-jammy");
    expect(getRuntime("dart")!.image).toBe("dart:3.9-sdk");
    expect(getRuntime("julia")!.image).toBe("julia:1.11");
    expect(getRuntime("haskell")!.image).toBe("haskell:9.8-slim");
  });
});
