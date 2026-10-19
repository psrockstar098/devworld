import { describe, expect, test } from "bun:test";
import { MemoryKv } from "../src/kv";
import { KvSubmissionStore } from "../src/submissions.kv";
import { InMemorySubmissionStore, hashFiles } from "../src/submissions";

const files = (n: number) => [{ path: "main.py", content: `print(${n})` }];

function store(max?: number) {
  const kv = new MemoryKv();
  return { kv, store: new KvSubmissionStore(kv, max) };
}

describe("KvSubmissionStore", () => {
  test("create stores a document with revision 1 and a hash", async () => {
    const { store: s } = store();
    const sub = await s.create("python", [], files(1), "2026-01-01T00:00:00.000Z");
    expect(sub.runtime).toBe("python");
    expect(sub.revisions).toHaveLength(1);
    expect(sub.revisions[0].hash).toBe(hashFiles(files(1)));
    expect(await s.get(sub.id)).toEqual(sub);
  });

  test("addRevision appends a numbered revision and persists it", async () => {
    const { store: s } = store();
    const sub = await s.create("python", [], files(1));
    const updated = await s.addRevision(sub.id, files(2));
    expect(updated?.revisions).toHaveLength(2);
    expect(updated?.revisions[1].n).toBe(2);
    expect((await s.get(sub.id))?.revisions).toHaveLength(2);
  });

  test("addRevision / setVerdict / compare on an unknown id return falsy", async () => {
    const { store: s } = store();
    expect(await s.addRevision("nope", files(1))).toBeUndefined();
    expect(await s.setVerdict("nope", 1, { passed: 0, total: 1 })).toBe(false);
    expect(await s.compare("nope", 1, 1)).toBeUndefined();
  });

  test("setVerdict records the verdict on the right revision", async () => {
    const { store: s } = store();
    const sub = await s.create("python", [], files(1));
    await s.addRevision(sub.id, files(2));
    expect(await s.setVerdict(sub.id, 2, { passed: 3, total: 4 })).toBe(true);
    expect(await s.setVerdict(sub.id, 9, { passed: 0, total: 1 })).toBe(false);
    const fresh = (await s.get(sub.id))!;
    expect(fresh.revisions[1].verdict).toEqual({ passed: 3, total: 4 });
    expect(fresh.revisions[0].verdict).toBeUndefined();
  });

  test("compare diffs the two revisions and reports the verdict delta", async () => {
    const { store: s } = store();
    const sub = await s.create("python", [], [{ path: "main.py", content: "a\nb\n" }]);
    await s.addRevision(sub.id, [{ path: "main.py", content: "a\nc\n" }]);
    await s.setVerdict(sub.id, 1, { passed: 5, total: 10 });
    await s.setVerdict(sub.id, 2, { passed: 8, total: 10 });
    const diff = await s.compare(sub.id, 1, 2);
    expect(diff?.added).toBe(1);
    expect(diff?.removed).toBe(1);
    expect(diff?.verdict.deltaPassed).toBe(3);
    expect(diff?.files[0].status).toBe("changed");
  });

  test("list is newest-first across many submissions", async () => {
    const { store: s } = store();
    const a = await s.create("python", [], files(1), "2026-01-01T00:00:00.000Z");
    const b = await s.create("python", [], files(2), "2026-01-02T00:00:00.000Z");
    const c = await s.create("python", [], files(3), "2026-01-03T00:00:00.000Z");
    const list = await s.list(10);
    expect(list.map((x) => x.id)).toEqual([c.id, b.id, a.id]);
    expect(await s.list(2)).toHaveLength(2);
    expect(await s.list(0)).toEqual([]);
  });

  test("create evicts the oldest submission beyond the cap", async () => {
    const { store: s, kv } = store(3);
    const ids: string[] = [];
    for (let i = 1; i <= 5; i++) {
      const sub = await s.create("python", [], files(i), `2026-01-0${i}T00:00:00.000Z`);
      ids.push(sub.id);
    }
    expect(await s.get(ids[0])).toBeUndefined();
    expect(await s.get(ids[1])).toBeUndefined();
    const remaining = await s.list(10);
    expect(remaining.map((x) => x.id)).toEqual([ids[4], ids[3], ids[2]]);
    // Eviction removes the document too, not just the index entry.
    expect(await kv.get(`submission:${ids[0]}`)).toBeNull();
    expect(await kv.get(`submission:${ids[4]}`)).not.toBeNull();
  });

  test("parity: the KV store and the in-memory store agree on shape", async () => {
    const { store: kvStore } = store();
    const memStore = new InMemorySubmissionStore();
    for (const s of [kvStore, memStore]) {
      const sub = await s.create("python", ["requests"], files(1), "2026-01-01T00:00:00.000Z");
      await s.addRevision(sub.id, files(2), "2026-01-01T00:01:00.000Z");
      await s.setVerdict(sub.id, 2, { passed: 1, total: 1 });
      const fresh = (await s.get(sub.id))!;
      expect(fresh.runtime).toBe("python");
      expect(fresh.dependencies).toEqual(["requests"]);
      expect(fresh.revisions.map((r) => r.n)).toEqual([1, 2]);
      expect(fresh.revisions[1].verdict).toEqual({ passed: 1, total: 1 });
    }
  });
});
