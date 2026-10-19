/**
 * Minimal key-value client for the worker.
 *
 * The submission store persists to the shared RocksDB-backed store (Kvrocks),
 * which speaks the Redis protocol, so a tiny command surface is all we need.
 * `MemoryKv` backs tests.
 */

import Redis from "ioredis";

export interface Kv {
  get(key: string): Promise<string | null>;
  set(key: string, value: string): Promise<void>;
  del(key: string): Promise<void>;
  /** Sorted-set add; used to keep a recency index of submissions. */
  zadd(key: string, score: number, member: string): Promise<void>;
  /** Members by descending score, inclusive range. */
  zrevrange(key: string, start: number, stop: number): Promise<string[]>;
  /** Remove members by ascending rank range (negatives count from the top). */
  zremrangebyrank(key: string, start: number, stop: number): Promise<void>;
  mget(keys: string[]): Promise<(string | null)[]>;
  ping(): Promise<boolean>;
  close(): Promise<void>;
}

export class RedisKv implements Kv {
  private readonly client: Redis;

  constructor(url: string) {
    this.client = new Redis(url, { maxRetriesPerRequest: 2 });
    this.client.on("error", () => {});
  }

  get(key: string): Promise<string | null> {
    return this.client.get(key);
  }

  async set(key: string, value: string): Promise<void> {
    await this.client.set(key, value);
  }

  async del(key: string): Promise<void> {
    await this.client.del(key);
  }

  async zadd(key: string, score: number, member: string): Promise<void> {
    await this.client.zadd(key, String(score), member);
  }

  zrevrange(key: string, start: number, stop: number): Promise<string[]> {
    return this.client.zrevrange(key, start, stop);
  }

  async zremrangebyrank(key: string, start: number, stop: number): Promise<void> {
    await this.client.zremrangebyrank(key, start, stop);
  }

  mget(keys: string[]): Promise<(string | null)[]> {
    if (keys.length === 0) return Promise.resolve([]);
    return this.client.mget(...keys);
  }

  async ping(): Promise<boolean> {
    try {
      return (await this.client.ping()) === "PONG";
    } catch {
      return false;
    }
  }

  async close(): Promise<void> {
    try {
      await this.client.quit();
    } catch {
      this.client.disconnect();
    }
  }
}

/** In-memory Kv for tests, with Redis-compatible zset rank semantics. */
export class MemoryKv implements Kv {
  private readonly map = new Map<string, string>();
  private readonly zsets = new Map<string, Map<string, number>>();

  async get(key: string): Promise<string | null> {
    return this.map.get(key) ?? null;
  }

  async set(key: string, value: string): Promise<void> {
    this.map.set(key, value);
  }

  async del(key: string): Promise<void> {
    this.map.delete(key);
    this.zsets.delete(key);
  }

  private z(key: string): Map<string, number> {
    let s = this.zsets.get(key);
    if (!s) {
      s = new Map();
      this.zsets.set(key, s);
    }
    return s;
  }

  async zadd(key: string, score: number, member: string): Promise<void> {
    this.z(key).set(member, score);
  }

  async zrevrange(key: string, start: number, stop: number): Promise<string[]> {
    const sorted = [...this.z(key).entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? 1 : -1));
    return sliceRank(sorted, start, stop).map(([m]) => m);
  }

  async zremrangebyrank(key: string, start: number, stop: number): Promise<void> {
    const s = this.z(key);
    const sorted = [...s.entries()].sort((a, b) => a[1] - b[1] || (a[0] < b[0] ? -1 : 1));
    for (const [m] of sliceRank(sorted, start, stop)) s.delete(m);
  }

  async mget(keys: string[]): Promise<(string | null)[]> {
    return keys.map((k) => this.map.get(k) ?? null);
  }

  async ping(): Promise<boolean> {
    return true;
  }

  async close(): Promise<void> {}
}

/** Apply Redis' inclusive, negative-capable rank range to an array. */
function sliceRank<T>(items: T[], start: number, stop: number): T[] {
  const n = items.length;
  let s = start < 0 ? n + start : start;
  if (s < 0) s = 0;
  let e = stop < 0 ? n + stop : stop;
  // Match Redis: a negative stop below the start yields an empty range rather
  // than being clamped to 0 (so `0, -11` on a 2-element list removes nothing).
  if (s > e || s >= n) return [];
  if (e >= n) e = n - 1;
  return items.slice(s, e + 1);
}
