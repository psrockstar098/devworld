import { describe, expect, test } from "bun:test";
import {
  compareRevisions,
  diffLines,
  hashFiles,
  InMemorySubmissionStore,
  type Revision,
} from "../src/submissions";
import { createApp } from "../src/app";
import { loadRuntimes } from "../src/runtimes";
import { FakeRunner, echoRunner } from "../src/runner";

const runtimes = loadRuntimes(new URL("../runtimes.json", import.meta.url));

function app(runner = echoRunner()) {
  return createApp({
    apiKey: "test-key",
    runtimes,
    runner,
    quotaPerHour: 1000,
    maxConcurrent: 4,
    maxRequestBytes: 1024 * 1024,
  });
}

const headers = { "content-type": "application/json", "x-api-key": "test-key" };

describe("content hashing", () => {
  test("is stable and independent of file order", () => {
    const a = [
      { path: "a.py", content: "x\n" },
      { path: "b.py", content: "y\n" },
    ];
    const b = [...a].reverse();
    expect(hashFiles(a)).toBe(hashFiles(b));
    expect(hashFiles(a)).not.toBe(hashFiles([{ path: "a.py", content: "z\n" }]));
  });

  test("changes when a path changes", () => {
    expect(hashFiles([{ path: "a.py", content: "x" }])).not.toBe(
      hashFiles([{ path: "b.py", content: "x" }]),
    );
  });
});

describe("line diff", () => {
  test("marks additions, removals and context", () => {
    const lines = diffLines("a\nb\nc", "a\nB\nc\nd");
    const kinds = lines.map((l) => l.kind);
    expect(kinds).toContain("del");
    expect(kinds).toContain("add");
    expect(kinds).toContain("ctx");
    expect(lines.filter((l) => l.kind === "del").map((l) => l.text)).toContain("b");
    expect(lines.filter((l) => l.kind === "add").map((l) => l.text)).toContain("d");
  });

  test("identical text is all context", () => {
    const lines = diffLines("same\ntext", "same\ntext");
    expect(lines.every((l) => l.kind === "ctx")).toBe(true);
  });

  test("falls back to a coarse diff past the line cap", () => {
    const big = Array.from({ length: 10 }, (_, i) => `line ${i}`).join("\n");
    const other = Array.from({ length: 10 }, (_, i) => `other ${i}`).join("\n");
    const lines = diffLines(big, other, 3);
    expect(lines.some((l) => l.kind === "del")).toBe(true);
    expect(lines.some((l) => l.kind === "add")).toBe(true);
  });
});

describe("submission store", () => {
  test("creates revision 1 and appends revisions", () => {
    const store = new InMemorySubmissionStore();
    const sub = store.create("python", [], [{ path: "main.py", content: "v1" }]);
    expect(sub.revisions.length).toBe(1);
    expect(sub.revisions[0].n).toBe(1);
    const updated = store.addRevision(sub.id, [{ path: "main.py", content: "v2" }]);
    expect(updated?.revisions.length).toBe(2);
    expect(updated?.revisions[1].n).toBe(2);
    expect(store.addRevision("nope", [])).toBeUndefined();
  });

  test("records and compares verdicts", () => {
    const store = new InMemorySubmissionStore();
    const sub = store.create("python", [], [{ path: "main.py", content: "v1" }]);
    store.setVerdict(sub.id, 1, { passed: 1, total: 2 });
    store.addRevision(sub.id, [{ path: "main.py", content: "v2" }]);
    store.setVerdict(sub.id, 2, { passed: 2, total: 2 });
    const diff = store.compare(sub.id, 1, 2);
    expect(diff?.added).toBeGreaterThanOrEqual(1);
    expect(diff?.verdict.before).toEqual({ passed: 1, total: 2 });
    expect(diff?.verdict.after).toEqual({ passed: 2, total: 2 });
    expect(diff?.verdict.deltaPassed).toBe(1);
    expect(store.compare(sub.id, 1, 99)).toBeUndefined();
  });

  test("evicts the oldest submission past the cap", () => {
    const store = new InMemorySubmissionStore();
    const first = store.create("python", [], [{ path: "main.py", content: "1" }], "2020-01-01T00:00:00Z");
    for (let i = 0; i < 600; i++) {
      store.create("python", [], [{ path: "main.py", content: `${i}` }], new Date(1700000000000 + i).toISOString());
    }
    expect(store.get(first.id)).toBeUndefined();
    expect(store.list().length).toBeLessThanOrEqual(500);
  });
});

describe("compareRevisions", () => {
  test("reports added, removed, changed and unchanged files", () => {
    const a: Revision = {
      n: 1,
      at: "2026-01-01T00:00:00Z",
      hash: "h1",
      files: [
        { path: "keep.py", content: "same" },
        { path: "gone.py", content: "gone" },
        { path: "edit.py", content: "a\nb" },
      ],
    };
    const b: Revision = {
      n: 2,
      at: "2026-01-02T00:00:00Z",
      hash: "h2",
      files: [
        { path: "keep.py", content: "same" },
        { path: "edit.py", content: "a\nB" },
        { path: "new.py", content: "new" },
      ],
    };
    const diff = compareRevisions(a, b);
    const status = Object.fromEntries(diff.files.map((f) => [f.path, f.status]));
    expect(status["keep.py"]).toBe("unchanged");
    expect(status["gone.py"]).toBe("removed");
    expect(status["edit.py"]).toBe("changed");
    expect(status["new.py"]).toBe("added");
  });
});

describe("submission endpoints", () => {
  function post(a: ReturnType<typeof createApp>, path: string, body: unknown) {
    return a.fetch(
      new Request(`http://localhost${path}`, { method: "POST", headers, body: JSON.stringify(body) }),
    );
  }
  function get(a: ReturnType<typeof createApp>, path: string) {
    return a.fetch(new Request(`http://localhost${path}`, { headers }));
  }

  test("store, revise, and compare through the API", async () => {
    const a = app();
    const created = await post(a, "/submissions", {
      runtime: "python",
      files: [{ path: "main.py", content: "print(1)" }],
    });
    expect(created.status).toBe(200);
    const meta = await created.json();
    expect(meta.revisions.length).toBe(1);

    // Grade revision 2: the FakeRunner echoes stdin, so the expected output matches.
    const graded = await post(a, "/assess", {
      runtime: "python",
      files: [{ path: "main.py", content: "print(2)" }],
      submissionId: meta.id,
      tests: [{ name: "t", stdin: "ok\n", expected_stdout: "ok\n", hidden: false }],
    });
    expect(graded.status).toBe(200);
    const verdict = await graded.json();
    expect(verdict.revision).toBe(2);
    expect(verdict.passed).toBe(1);

    const detail = await (await get(a, `/submissions/${meta.id}`)).json();
    expect(detail.revisions.length).toBe(2);
    expect(detail.revisions[1].verdict).toEqual({ passed: 1, total: 1 });

    const diff = await (await get(a, `/submissions/${meta.id}/compare?from=1&to=2`)).json();
    expect(diff.from).toBe(1);
    expect(diff.to).toBe(2);
    expect(diff.verdict.deltaPassed).toBe(1);

    const rev = await (await get(a, `/submissions/${meta.id}/revisions/1`)).json();
    expect(rev.files[0].path).toBe("main.py");
  });

  test("unknown submission and revision are 404", async () => {
    const a = app();
    expect((await get(a, "/submissions/deadbeefdeadbeef")).status).toBe(404);
    const created = await (await post(a, "/submissions", { runtime: "python", files: [{ path: "main.py", content: "x" }] })).json();
    expect((await get(a, `/submissions/${created.id}/revisions/9`)).status).toBe(404);
    expect((await get(a, `/submissions/${created.id}/compare?from=1&to=9`)).status).toBe(404);
  });

  test("grading an unknown submission id is 404", async () => {
    const res = await post(app(), "/assess", {
      runtime: "python",
      files: [{ path: "main.py", content: "x" }],
      submissionId: "deadbeefdeadbeef",
      tests: [{ name: "t", stdin: "", expected_stdout: "x", hidden: false }],
    });
    expect(res.status).toBe(404);
  });

  test("the capped FakeRunner proves the diff catches a regression", async () => {
    // A runner that passes revision 1 and fails revision 2.
    let call = 0;
    const runner = new FakeRunner(() => ({
      stdout: call++ === 0 ? "ok" : "nope",
      stderr: "",
      exitCode: 0,
      timedOut: false,
      truncated: false,
      durationMs: 1,
      artifacts: [],
    }));
    const a = app(runner);
    const meta = await (await post(a, "/submissions", { runtime: "python", files: [{ path: "main.py", content: "v1" }] })).json();
    // POST /submissions is revision 1; the two assessments become revisions 2 and 3.
    await post(a, "/assess", {
      runtime: "python",
      files: [{ path: "main.py", content: "v1" }],
      submissionId: meta.id,
      tests: [{ name: "t", stdin: "", expected_stdout: "ok", hidden: false }],
    });
    await post(a, "/assess", {
      runtime: "python",
      files: [{ path: "main.py", content: "v2" }],
      submissionId: meta.id,
      tests: [{ name: "t", stdin: "", expected_stdout: "ok", hidden: false }],
    });
    const diff = await (await get(a, `/submissions/${meta.id}/compare?from=2&to=3`)).json();
    expect(diff.verdict.deltaPassed).toBe(-1);
  });
});
