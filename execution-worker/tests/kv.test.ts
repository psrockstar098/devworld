import { describe, expect, test } from "bun:test";
import { MemoryKv } from "../src/kv";

describe("MemoryKv strings", () => {
  test("set / get / del / mget", async () => {
    const kv = new MemoryKv();
    expect(await kv.get("a")).toBeNull();
    await kv.set("a", "1");
    await kv.set("b", "2");
    expect(await kv.get("a")).toBe("1");
    expect(await kv.mget(["a", "b", "c"])).toEqual(["1", "2", null]);
    expect(await kv.mget([])).toEqual([]);
    await kv.del("a");
    expect(await kv.get("a")).toBeNull();
    expect(await kv.ping()).toBe(true);
  });
});

describe("MemoryKv sorted sets", () => {
  test("zrevrange returns members by descending score", async () => {
    const kv = new MemoryKv();
    await kv.zadd("z", 1, "one");
    await kv.zadd("z", 3, "three");
    await kv.zadd("z", 2, "two");
    expect(await kv.zrevrange("z", 0, -1)).toEqual(["three", "two", "one"]);
    expect(await kv.zrevrange("z", 0, 1)).toEqual(["three", "two"]);
    expect(await kv.zrevrange("z", 0, 9)).toEqual(["three", "two", "one"]);
  });

  test("zadd updates the score of an existing member", async () => {
    const kv = new MemoryKv();
    await kv.zadd("z", 1, "a");
    await kv.zadd("z", 5, "a");
    expect(await kv.zrevrange("z", 0, -1)).toEqual(["a"]);
  });

  test("zremrangebyrank keeps the newest N via a negative stop", async () => {
    const kv = new MemoryKv();
    for (const [score, m] of [
      [1, "a"],
      [2, "b"],
      [3, "c"],
      [4, "d"],
      [5, "e"],
    ] as const) {
      await kv.zadd("z", score, m);
    }
    // Keep the top 3: remove ranks 0..-(3+1).
    await kv.zremrangebyrank("z", 0, -4);
    expect(await kv.zrevrange("z", 0, -1)).toEqual(["e", "d", "c"]);
  });

  test("zremrangebyrank is a no-op when the set is smaller than the keep size", async () => {
    const kv = new MemoryKv();
    await kv.zadd("z", 1, "a");
    await kv.zadd("z", 2, "b");
    await kv.zremrangebyrank("z", 0, -11);
    expect(await kv.zrevrange("z", 0, -1)).toEqual(["b", "a"]);
  });
});
