// presence.ts — who is online right now, and where.
//
// Real players only: an entry is keyed by the player id resolved from the
// caller's own Bearer token, so a player can only ever report their own
// presence. Presence lives in the disposable cache (Redis), not the durable
// store: a player who stops heartbeating simply fades from the list.
// Stale entries (no heartbeat within PRESENCE_STALE_MS) are pruned on every
// read and write, so the list never shows ghosts.

import type { KvStore } from "./kv";

export interface PresenceEntry {
  playerId: string;
  city: string;
  map: string;
  lastSeenMs: number;
}

/** No heartbeat within this long and the player counts as offline. */
export const PRESENCE_STALE_MS = 120_000;
/** Whole-document TTL; heartbeats refresh it on every write. */
const PRESENCE_DOC_TTL_SEC = 300;
const DOC_KEY = "presence:online";
const MAX_ENTRIES = 200;

export class PresenceStore {
  constructor(
    private readonly cache: KvStore,
    private readonly now: () => number = () => Date.now(),
  ) {}

  private async readDoc(): Promise<Record<string, PresenceEntry>> {
    const raw = await this.cache.get(DOC_KEY);
    if (!raw) return {};
    try {
      const doc = JSON.parse(raw) as Record<string, PresenceEntry>;
      if (doc && typeof doc === "object" && !Array.isArray(doc)) return doc;
    } catch {
      // Corrupt document: start fresh rather than serving bad state.
    }
    return {};
  }

  private async writeDoc(doc: Record<string, PresenceEntry>): Promise<void> {
    await this.cache.set(DOC_KEY, JSON.stringify(doc), PRESENCE_DOC_TTL_SEC);
  }

  private prune(doc: Record<string, PresenceEntry>, nowMs: number): Record<string, PresenceEntry> {
    const out: Record<string, PresenceEntry> = {};
    for (const [id, e] of Object.entries(doc)) {
      if (e && typeof e === "object" && typeof e.lastSeenMs === "number" && nowMs - e.lastSeenMs <= PRESENCE_STALE_MS) {
        out[id] = e;
      }
    }
    return out;
  }

  async heartbeat(playerId: string, city: string, map: string): Promise<PresenceEntry> {
    const nowMs = this.now();
    const doc = this.prune(await this.readDoc(), nowMs);
    const entry: PresenceEntry = { playerId, city, map, lastSeenMs: nowMs };
    doc[playerId] = entry;
    const ids = Object.keys(doc);
    if (ids.length > MAX_ENTRIES) {
      ids.sort((a, b) => doc[a].lastSeenMs - doc[b].lastSeenMs);
      for (const id of ids.slice(0, ids.length - MAX_ENTRIES)) delete doc[id];
    }
    await this.writeDoc(doc);
    return entry;
  }

  async listOnline(): Promise<PresenceEntry[]> {
    const doc = this.prune(await this.readDoc(), this.now());
    return Object.values(doc).sort((a, b) => b.lastSeenMs - a.lastSeenMs);
  }
}
