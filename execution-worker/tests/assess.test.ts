import { describe, expect, test } from "bun:test";
import { createApp } from "../src/app";
import { loadRuntimes } from "../src/runtimes";
import { echoRunner, FakeRunner, type RunResult } from "../src/runner";

const runtimes = loadRuntimes(new URL("../runtimes.json", import.meta.url));
const headers = { "content-type": "application/json", "x-api-key": "test-key" };
const pyFile = { path: "main.py", content: "print('hi')" };

function makeApp(quotaPerHour: number, runner = echoRunner()) {
  return createApp({
    apiKey: "test-key",
    runtimes,
    runner,
    quotaPerHour,
    maxConcurrent: 4,
    maxRequestBytes: 1024 * 1024,
  });
}

function execute(app: ReturnType<typeof makeApp>) {
  return app.fetch(
    new Request("http://localhost/execute", {
      method: "POST",
      headers,
      body: JSON.stringify({ runtime: "python", files: [pyFile] }),
    }),
  );
}

describe("quota enforcement", () => {
  test("allows up to the hourly quota then returns 429", async () => {
    const app = makeApp(3);
    expect((await execute(app)).status).toBe(200);
    expect((await execute(app)).status).toBe(200);
    expect((await execute(app)).status).toBe(200);
    const limited = await execute(app);
    expect(limited.status).toBe(429);
    const body = await limited.json();
    expect(body.error).toMatch(/quota exceeded/);
    expect(typeof body.retryAfterSec).toBe("number");
    expect(limited.headers.get("retry-after")).not.toBeNull();
  });

  test("assess requests also consume quota", async () => {
    const app = makeApp(2);
    const assessBody = {
      runtime: "python",
      files: [pyFile],
      tests: [{ name: "t", stdin: "", expected_stdout: "" }],
    };
    const one = await app.fetch(
      new Request("http://localhost/assess", { method: "POST", headers, body: JSON.stringify(assessBody) }),
    );
    expect(one.status).toBe(200);
    expect((await execute(app)).status).toBe(200);
    expect((await execute(app)).status).toBe(429);
  });
});

describe("assess with mocked programs", () => {
  // Fake program: reads stdin, echoes back uppercased.
  const upperRunner = new FakeRunner(
    (opts): RunResult => ({
      stdout: opts.stdin.toUpperCase(),
      stderr: "",
      exitCode: 0,
      timedOut: false,
      truncated: false,
      durationMs: 1,
      artifacts: [],
    }),
  );

  function assess(app: ReturnType<typeof makeApp>, tests: unknown[]) {
    return app.fetch(
      new Request("http://localhost/assess", {
        method: "POST",
        headers,
        body: JSON.stringify({ runtime: "python", files: [pyFile], tests }),
      }),
    );
  }

  test("passes matching tests, reports actual for visible ones", async () => {
    const app = makeApp(100, upperRunner);
    const res = await assess(app, [
      { name: "shout", stdin: "hello\n", expected_stdout: "HELLO\n", hidden: false },
      { name: "fail-case", stdin: "hey\n", expected_stdout: "wrong\n", hidden: false },
    ]);
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body.passed).toBe(1);
    expect(body.total).toBe(2);
    expect(body.results[0]).toMatchObject({ name: "shout", passed: true, hidden: false });
    expect(body.results[0].actual).toBe("HELLO");
    expect(body.results[1].passed).toBe(false);
    expect(body.results[1].errorKind).toBe("wrong-output");
    expect(body.results[1].actual).toBe("HEY");
  });

  test("HIDDEN TEST LEAK RULE: response contains no expected/actual data for hidden tests", async () => {
    const app = makeApp(100, upperRunner);
    const secret = "s3cr3t-hidden-expected-value";
    const res = await assess(app, [
      { name: "hidden-1", stdin: "hidden-stdin-payload\n", expected_stdout: secret, hidden: true },
      { name: "hidden-2", stdin: "x\n", expected_stdout: "X\n", hidden: true },
    ]);
    expect(res.status).toBe(200);
    const raw = await res.text();
    // The raw response body must not contain any hidden test data.
    expect(raw).not.toContain(secret);
    expect(raw).not.toContain("hidden-stdin-payload");
    expect(raw).not.toContain("expected_stdout");
    expect(raw).not.toContain("HIDDEN-STDIN-PAYLOAD"); // nor the actual output derived from it
    const body = JSON.parse(raw);
    expect(body.results[0]).toEqual({ name: "hidden-1", passed: false, hidden: true, errorKind: "wrong-output" });
    expect(body.results[1].passed).toBe(true);
    expect(body.results[1].hidden).toBe(true);
    expect("actual" in body.results[1]).toBe(false);
  });

  test("hidden timeout leaks nothing either", async () => {
    const slow = new FakeRunner(
      (): RunResult => ({
        stdout: "partial-secret-output",
        stderr: "secret diagnostics",
        exitCode: 137,
        timedOut: true,
        truncated: false,
        durationMs: 1000,
        artifacts: [],
      }),
    );
    const app = makeApp(100, slow);
    const res = await assess(app, [
      { name: "h", stdin: "in", expected_stdout: "expected-secret", hidden: true },
    ]);
    const raw = await res.text();
    expect(raw).not.toContain("partial-secret-output");
    expect(raw).not.toContain("secret diagnostics");
    expect(raw).not.toContain("expected-secret");
    const body = JSON.parse(raw);
    expect(body.results[0]).toEqual({ name: "h", passed: false, hidden: true, errorKind: "timeout" });
  });

  test("hidden build failure reports kind only", async () => {
    const broken = new FakeRunner(
      (): RunResult => ({
        stdout: "",
        stderr: "secret.java:1: error: ';' expected",
        exitCode: 1,
        timedOut: false,
        truncated: false,
        durationMs: 5,
        artifacts: [],
      }),
    );
    const app = makeApp(100, broken);
    const res = await app.fetch(
      new Request("http://localhost/assess", {
        method: "POST",
        headers,
        body: JSON.stringify({
          runtime: "java",
          files: [{ path: "Main.java", content: "broken" }],
          tests: [{ name: "h", stdin: "", expected_stdout: "", hidden: true }],
        }),
      }),
    );
    const raw = await res.text();
    expect(raw).not.toContain("';' expected");
    const body = JSON.parse(raw);
    expect(body.results[0].errorKind).toBe("build-failed");
  });

  test("visible build failure includes diagnostics", async () => {
    const broken = new FakeRunner(
      (): RunResult => ({
        stdout: "",
        stderr: "main.c:1:1: error: expected ';'",
        exitCode: 1,
        timedOut: false,
        truncated: false,
        durationMs: 5,
        artifacts: [],
      }),
    );
    const app = makeApp(100, broken);
    const res = await app.fetch(
      new Request("http://localhost/assess", {
        method: "POST",
        headers,
        body: JSON.stringify({
          runtime: "c",
          files: [{ path: "main.c", content: "broken" }],
          tests: [{ name: "v", stdin: "", expected_stdout: "", hidden: false }],
        }),
      }),
    );
    const body = await res.json();
    expect(body.results[0].errorKind).toBe("build-failed");
    expect(body.results[0].error).toMatch(/expected ';'/);
  });

  test("uses fresh stdin per test", async () => {
    const seen: string[] = [];
    const app = makeApp(
      100,
      new FakeRunner((opts): RunResult => {
        seen.push(opts.stdin);
        return { stdout: opts.stdin, stderr: "", exitCode: 0, timedOut: false, truncated: false, durationMs: 1, artifacts: [] };
      }),
    );
    await assess(app, [
      { name: "a", stdin: "one", expected_stdout: "one" },
      { name: "b", stdin: "two", expected_stdout: "two" },
    ]);
    expect(seen).toEqual(["one", "two"]);
  });
});
