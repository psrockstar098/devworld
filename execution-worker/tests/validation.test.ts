import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { loadRuntimes } from "../src/runtimes";
import { FakeRunner, echoRunner, type RunResult } from "../src/runner";

const runtimes = loadRuntimes(new URL("../runtimes.json", import.meta.url));

function appWith(runner: FakeRunner, overrides: Record<string, unknown> = {}) {
  return createApp({
    apiKey: "test-key",
    runtimes,
    runner,
    quotaPerHour: 1000,
    maxConcurrent: 4,
    maxRequestBytes: 1024 * 1024,
    ...overrides,
  } as Parameters<typeof createApp>[0]);
}

const headers = { "content-type": "application/json", "x-api-key": "test-key" };
const pyFile = { path: "main.py", content: "print('hi')" };

function post(app: ReturnType<typeof createApp>, path: string, body: unknown, hdrs = headers) {
  return app.fetch(
    new Request(`http://localhost${path}`, {
      method: "POST",
      headers: hdrs,
      body: JSON.stringify(body),
    }),
  );
}

describe("validation", () => {
  test("rejects unknown runtime", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", { runtime: "cobol", files: [pyFile] });
    expect(res.status).toBe(400);
    expect((await res.json()).error).toMatch(/unknown runtime/);
  });

  test("rejects malformed runtime id", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", { runtime: "../evil", files: [pyFile] });
    expect(res.status).toBe(400);
  });

  test("rejects empty files array", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", { runtime: "python", files: [] });
    expect(res.status).toBe(400);
  });

  test("rejects path traversal", async () => {
    const app = appWith(echoRunner());
    for (const bad of ["../evil.py", "/abs.py", "a/../../b.py", "x\\y.py", "__stdin.txt"]) {
      const res = await post(app, "/execute", {
        runtime: "python",
        files: [{ path: bad, content: "x" }],
      });
      expect(res.status).toBe(400);
    }
  });

  test("rejects duplicate paths", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", {
      runtime: "python",
      files: [pyFile, { path: "main.py", content: "y" }],
    });
    expect(res.status).toBe(400);
  });

  test("rejects oversized file with 413", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", {
      runtime: "python",
      files: [{ path: "main.py", content: "x".repeat(300 * 1024) }],
    });
    expect(res.status).toBe(413);
  });

  test("rejects oversized stdin with 413", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", {
      runtime: "python",
      files: [pyFile],
      stdin: "x".repeat(100 * 1024),
    });
    expect(res.status).toBe(413);
  });

  test("rejects out-of-range timeout", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/execute", { runtime: "python", files: [pyFile], timeout: 999 });
    expect(res.status).toBe(400);
  });

  test("rejects invalid JSON body", async () => {
    const app = appWith(echoRunner());
    const res = await app.fetch(
      new Request("http://localhost/execute", {
        method: "POST",
        headers,
        body: "{not json",
      }),
    );
    expect(res.status).toBe(400);
  });

  test("assess requires tests", async () => {
    const app = appWith(echoRunner());
    const res = await post(app, "/assess", { runtime: "python", files: [pyFile], tests: [] });
    expect(res.status).toBe(400);
  });

  test("assess rejects too many tests", async () => {
    const app = appWith(echoRunner());
    const tests = Array.from({ length: 25 }, (_, i) => ({
      name: `t${i}`,
      stdin: "",
      expected_stdout: "",
    }));
    const res = await post(app, "/assess", { runtime: "python", files: [pyFile], tests });
    expect(res.status).toBe(400);
  });

  test("missing api key -> 401", async () => {
    const app = appWith(echoRunner());
    const res = await app.fetch(
      new Request("http://localhost/runtimes", { headers: { "content-type": "application/json" } }),
    );
    expect(res.status).toBe(401);
  });

  test("wrong api key -> 401", async () => {
    const app = appWith(echoRunner());
    const res = await post(
      app,
      "/execute",
      { runtime: "python", files: [pyFile] },
      { "content-type": "application/json", "x-api-key": "wrong" },
    );
    expect(res.status).toBe(401);
  });

  test("unknown route -> 404", async () => {
    const app = appWith(echoRunner());
    const res = await app.fetch(new Request("http://localhost/nope", { headers }));
    expect(res.status).toBe(404);
  });
});

describe("execute happy path (mocked runner)", () => {
  test("returns runner result fields", async () => {
    const canned: RunResult = {
      stdout: "hi\n",
      stderr: "",
      exitCode: 0,
      timedOut: false,
      truncated: false,
      durationMs: 12,
      artifacts: [{ path: "out.txt", base64: Buffer.from("data").toString("base64") }],
    };
    const app = appWith(new FakeRunner(() => canned));
    const res = await post(app, "/execute", { runtime: "python", files: [pyFile], stdin: "in\n" });
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body).toMatchObject({
      runtime: "python",
      stdout: "hi\n",
      stderr: "",
      exitCode: 0,
      timedOut: false,
      truncated: false,
    });
    expect(body.artifacts).toHaveLength(1);
  });

  test("passes timeout through to runner (explicit and default)", async () => {
    const seen: number[] = [];
    const app = appWith(
      new FakeRunner((opts) => {
        seen.push(opts.timeoutSec);
        return { stdout: "", stderr: "", exitCode: 0, timedOut: false, truncated: false, durationMs: 1, artifacts: [] };
      }),
    );
    await post(app, "/execute", { runtime: "python", files: [pyFile], timeout: 7 });
    await post(app, "/execute", { runtime: "python", files: [pyFile] });
    expect(seen[0]).toBe(7);
    expect(seen[1]).toBe(10); // python default from registry
  });
});
