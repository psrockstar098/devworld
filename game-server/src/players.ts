// players.ts — durable player records in Kvrocks (RocksDB).
//
// This is *current state*: who the player is and what they have completed. XP
// and money are append-only in ClickHouse; the record here holds the fast-read
// identity plus completion flags.

import type { KvStore } from "./kv";

export interface PlayerRecord {
  id: string;
  handle: string;
  displayName: string;
  createdAt: string;
  updatedAt: string;
  completedMissions: string[];
  flags: Record<string, unknown>;
}

export class PlayerError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "PlayerError";
    this.status = status;
  }
}

const HANDLE_RE = /^[a-z0-9_]{3,24}$/;
const ID_RE = /^[A-Za-z0-9_-]{6,64}$/;

export function validateHandle(handle: string): void {
  if (!HANDLE_RE.test(handle)) {
    throw new PlayerError(400, "handle must be 3-24 chars of a-z, 0-9 or _");
  }
}

export function validatePlayerId(id: string): void {
  if (!ID_RE.test(id)) throw new PlayerError(400, "invalid player id");
}

export class PlayerStore {
  constructor(private readonly kv: KvStore) {}

  static key(id: string): string {
    return `player:${id}`;
  }

  async create(input: { id: string; handle: string; displayName?: string }): Promise<PlayerRecord> {
    validatePlayerId(input.id);
    validateHandle(input.handle);
    if (await this.get(input.id)) throw new PlayerError(409, "player already exists");
    const handleKey = PlayerStore.handleKey(input.handle);
    if (await this.kv.get(handleKey)) throw new PlayerError(409, "handle already taken");
    const now = new Date().toISOString();
    const rec: PlayerRecord = {
      id: input.id,
      handle: input.handle,
      displayName: (input.displayName ?? input.handle).slice(0, 32),
      createdAt: now,
      updatedAt: now,
      completedMissions: [],
      flags: {},
    };
    await this.kv.set(PlayerStore.key(rec.id), JSON.stringify(rec));
    await this.kv.set(handleKey, rec.id);
    return rec;
  }

  static handleKey(handle: string): string {
    return `handle:${handle}`;
  }

  async get(id: string): Promise<PlayerRecord | null> {
    if (!ID_RE.test(id)) return null;
    const raw = await this.kv.get(PlayerStore.key(id));
    if (!raw) return null;
    try {
      return JSON.parse(raw) as PlayerRecord;
    } catch {
      return null;
    }
  }

  async save(rec: PlayerRecord): Promise<void> {
    rec.updatedAt = new Date().toISOString();
    await this.kv.set(PlayerStore.key(rec.id), JSON.stringify(rec));
  }

  async markCompleted(id: string, missionId: string): Promise<PlayerRecord | null> {
    const rec = await this.get(id);
    if (!rec) return null;
    if (!rec.completedMissions.includes(missionId)) rec.completedMissions.push(missionId);
    await this.save(rec);
    return rec;
  }
}
