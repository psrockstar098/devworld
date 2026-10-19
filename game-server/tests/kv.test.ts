import { describe, expect, test } from "bun:test";
import { MemoryKv } from "../src/kv";

/** A MemoryKv with a controllable clock so TTL is deterministic. */
function clocked() {
  let now = 1_000_000;
  const kv = new MemoryKv(() => now);
  return { kv, advance: (ms: number) => (now += ms) };
}

describe("MemoryKv", () => {
  test("set / get / del round trip", async () => {
    const kv = new MemoryKv();
    expect(await kv.get("missing")).toBeNull();
    await kv.set("k", "v");
    expect(await kv.get("k")).toBe("v");
    await kv.del("k");
    expect(await kv.get("k")).toBeNull();
  });

  test("ttl reports -1 without expiry and -2 for a missing key", async () => {
    const kv = new MemoryKv();
    await kv.set("k", "v");
    expect(await kv.ttl("k")).toBe(-1);
    expect(await kv.ttl("nope")).toBe(-2);
  });

  test("keys expire at their ttl", async () => {
    const { kv, advance } = clocked();
    await kv.set("k", "v", 10);
    expect(await kv.ttl("k")).toBe(10);
    advance(9_000);
    expect(await kv.get("k")).toBe("v");
    advance(1_000);
    expect(await kv.get("k")).toBeNull();
    expect(await kv.ttl("k")).toBe(-2);
  });

  test("expire applies to an existing key and is a no-op otherwise", async () => {
    const { kv, advance } = clocked();
    await kv.set("a", "1");
    await kv.expire("a", 5);
    expect(await kv.ttl("a")).toBe(5);
    advance(5_000);
    expect(await kv.get("a")).toBeNull();
    await expect(kv.expire("missing", 5)).resolves.toBeUndefined();
  });

  test("incr creates and preserves ttl", async () => {
    const { kv } = clocked();
    expect(await kv.incr("n")).toBe(1);
    expect(await kv.incr("n", 4)).toBe(5);
    await kv.expire("n", 30);
    expect(await kv.incr("n")).toBe(6);
    expect(await kv.ttl("n")).toBe(30);
  });

  test("incr on an expired key restarts from zero", async () => {
    const { kv, advance } = clocked();
    await kv.set("n", "100", 1);
    advance(1_000);
    expect(await kv.incr("n")).toBe(1);
  });

  test("ping is always true", async () => {
    expect(await new MemoryKv().ping()).toBe(true);
  });
});
