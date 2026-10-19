// kv.ts — durable key-value access (Kvrocks / RocksDB) and the cache (Redis).
//
// Both stores speak the Redis protocol, so one interface covers them: the
// `kv` client points at Kvrocks for durable current-state, the `cache` client
// points at Redis for TTL'd, disposable values. Tests use MemoryKv.

import Redis from "ioredis";

export interface KvStore {
  get(key: string): Promise<string | null>;
  set(key: string, value: string, ttlSec?: number): Promise<void>;
  del(key: string): Promise<void>;
  /** Atomic increment; creates the key at 0 first. */
  incr(key: string, by?: number): Promise<number>;
  expire(key: string, ttlSec: number): Promise<void>;
  /** Seconds to live, -1 if no expiry, -2 if the key does not exist. */
  ttl(key: string): Promise<number>;
  ping(): Promise<boolean>;
  close(): Promise<void>;
}

export class RedisKv implements KvStore {
  private readonly client: Redis;

  constructor(url: string, opts: { lazyConnect?: boolean } = {}) {
    this.client = new Redis(url, {
      lazyConnect: opts.lazyConnect ?? false,
      maxRetriesPerRequest: 2,
      enableOfflineQueue: true,
    });
    // Never crash the process on a transient connection error.
    this.client.on("error", () => {});
  }

  async get(key: string): Promise<string | null> {
    return this.client.get(key);
  }

  async set(key: string, value: string, ttlSec?: number): Promise<void> {
    if (ttlSec && ttlSec > 0) await this.client.set(key, value, "EX", ttlSec);
    else await this.client.set(key, value);
  }

  async del(key: string): Promise<void> {
    await this.client.del(key);
  }

  async incr(key: string, by = 1): Promise<number> {
    return by === 1 ? this.client.incr(key) : this.client.incrby(key, by);
  }

  async expire(key: string, ttlSec: number): Promise<void> {
    await this.client.expire(key, ttlSec);
  }

  async ttl(key: string): Promise<number> {
    return this.client.ttl(key);
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

interface MemEntry {
  value: string;
  expiresAt: number | null;
}

/** Deterministic in-memory store for unit tests (honours TTL). */
export class MemoryKv implements KvStore {
  private readonly map = new Map<string, MemEntry>();

  constructor(private readonly now: () => number = () => Date.now()) {}

  private live(key: string): MemEntry | undefined {
    const e = this.map.get(key);
    if (!e) return undefined;
    if (e.expiresAt !== null && e.expiresAt <= this.now()) {
      this.map.delete(key);
      return undefined;
    }
    return e;
  }

  async get(key: string): Promise<string | null> {
    return this.live(key)?.value ?? null;
  }

  async set(key: string, value: string, ttlSec?: number): Promise<void> {
    this.map.set(key, {
      value,
      expiresAt: ttlSec && ttlSec > 0 ? this.now() + ttlSec * 1000 : null,
    });
  }

  async del(key: string): Promise<void> {
    this.map.delete(key);
  }

  async incr(key: string, by = 1): Promise<number> {
    const n = Number(this.live(key)?.value ?? "0") + by;
    const existing = this.map.get(key);
    this.map.set(key, { value: String(n), expiresAt: existing?.expiresAt ?? null });
    return n;
  }

  async expire(key: string, ttlSec: number): Promise<void> {
    const e = this.live(key);
    if (e) e.expiresAt = this.now() + ttlSec * 1000;
  }

  async ttl(key: string): Promise<number> {
    const e = this.live(key);
    if (!e) return -2;
    if (e.expiresAt === null) return -1;
    return Math.max(0, Math.ceil((e.expiresAt - this.now()) / 1000));
  }

  async ping(): Promise<boolean> {
    return true;
  }

  async close(): Promise<void> {}
}
