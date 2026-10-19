import { describe, expect, test } from "bun:test";
import { DependencyError, DependencyPolicy, MAX_REQUESTED_DEPENDENCIES } from "../src/deps";
import { createApp } from "../src/app";
import { loadRuntimes } from "../src/runtimes";
import { echoRunner } from "../src/runner";

const policy = DependencyPolicy.load(new URL("../dependencies.json", import.meta.url));
const runtimes = loadRuntimes(new URL("../runtimes.json", import.meta.url));

function app(deps: DependencyPolicy = policy) {
  return createApp({
    apiKey: "test-key",
    runtimes,
    runner: echoRunner(),
    quotaPerHour: 1000,
    maxConcurrent: 4,
    maxRequestBytes: 1024 * 1024,
    deps,
  });
}

const headers = { "content-type": "application/json", "x-api-key": "test-key" };
const pyFile = { path: "main.py", content: "print('hi')" };

function post(a: ReturnType<typeof createApp>, path: string, body: unknown) {
  return a.fetch(
    new Request(`http://localhost${path}`, { method: "POST", headers, body: JSON.stringify(body) }),
  );
}

describe("dependency allowlist policy", () => {
  test("lists approved dependencies, optionally per runtime", () => {
    expect(policy.list().length).toBeGreaterThan(0);
    const py = policy.list("python");
    expect(py.length).toBeGreaterThan(0);
    expect(py.every((d) => d.runtime === "python")).toBe(true);
  });

  test("accepts an approved, baked dependency and pins the version", () => {
    const out = policy.resolve("typescript", ["typescript"]);
    expect(out.length).toBe(1);
    expect(out[0].name).toBe("typescript");
    expect(out[0].baked).toBe(true);
    expect(out[0].version).not.toContain("^");
  });

  test("deduplicates repeated requests", () => {
    const out = policy.resolve("typescript", ["typescript", "typescript"]);
    expect(out.length).toBe(1);
  });

  test("refuses an unknown dependency with 403", () => {
    try {
      policy.resolve("python", ["leftpad"]);
      throw new Error("should have thrown");
    } catch (e) {
      expect(e).toBeInstanceOf(DependencyError);
      expect((e as DependencyError).status).toBe(403);
      expect((e as DependencyError).message).toMatch(/not approved/);
    }
  });

  test("refuses an approved-but-not-baked dependency with 400", () => {
    try {
      policy.resolve("python", ["numpy"]);
      throw new Error("should have thrown");
    } catch (e) {
      expect(e).toBeInstanceOf(DependencyError);
      expect((e as DependencyError).status).toBe(400);
      expect((e as DependencyError).message).toMatch(/mirror/);
    }
  });

  test("rejects a bad dependency name", () => {
    expect(() => policy.resolve("python", [""])).toThrow(DependencyError);
  });

  test("the constructor refuses unpinned versions", () => {
    expect(() => new DependencyPolicy([{ runtime: "python", name: "x", version: "^1", baked: true }])).toThrow();
    expect(() => new DependencyPolicy([{ runtime: "python", name: "x", version: "*", baked: true }])).toThrow();
  });

  test("caps the number of requested dependencies", () => {
    expect(MAX_REQUESTED_DEPENDENCIES).toBe(10);
    const many = Array.from({ length: 11 }, (_, i) => `p${i}`);
    expect(() => policy.resolve("python", many)).toThrow(DependencyError);
  });
});

describe("dependency endpoints", () => {
  test("GET /dependencies returns the allowlist", async () => {
    const res = await app().fetch(new Request("http://localhost/dependencies", { headers }));
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(Array.isArray(body.approved)).toBe(true);
    expect(body.approved.length).toBe(policy.list().length);
  });

  test("GET /dependencies?runtime= filters", async () => {
    const res = await app().fetch(new Request("http://localhost/dependencies?runtime=python", { headers }));
    const body = await res.json();
    expect(body.approved.every((d: { runtime: string }) => d.runtime === "python")).toBe(true);
  });

  test("/execute rejects an unapproved dependency before running", async () => {
    const res = await post(app(), "/execute", {
      runtime: "python",
      files: [pyFile],
      dependencies: ["leftpad"],
    });
    expect(res.status).toBe(403);
    expect((await res.json()).error).toMatch(/not approved/);
  });

  test("/execute accepts an approved, baked dependency", async () => {
    const res = await post(app(), "/execute", {
      runtime: "typescript",
      files: [{ path: "main.ts", content: "console.log('hi')" }],
      dependencies: ["typescript"],
    });
    expect(res.status).toBe(200);
  });

  test("an empty policy approves nothing", async () => {
    const res = await post(app(new DependencyPolicy([])), "/execute", {
      runtime: "python",
      files: [pyFile],
      dependencies: ["numpy"],
    });
    expect(res.status).toBe(403);
  });
});
