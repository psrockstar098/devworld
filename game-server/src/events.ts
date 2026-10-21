// events.ts — the world event log.
//
// Every consequential thing that happens in the world gets one entry here:
// a company founded, a product launched, a business failed, a hackathon
// won, a data center outage. This log is the single source of truth for
// the in-game newspaper, the world timeline, and event-driven NPC dialogue
// — all three read it, none of them invent their own news.
//
// Events are durable (the kv store, not the cache): history must survive
// restarts. Writes are admin-key only for now; the server's own systems
// (companies, products, economy) will record through the same store as
// they land. Reads are authenticated but open to any player: history is
// public to the world's residents.

import type { KvStore } from "./kv";

export interface WorldEvent {
  id: string;
  type: string;
  title: string;
  detail: string;
  actorId: string | null;
  city: string | null;
  createdAtMs: number;
}

const DOC_KEY = "events:log";
const MAX_EVENTS = 1000;
const MAX_LIST = 100;

function randomId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

export class EventStore {
  constructor(
    private readonly kv: KvStore,
    private readonly now: () => number = () => Date.now(),
  ) {}

  private async readAll(): Promise<WorldEvent[]> {
    const raw = await this.kv.get(DOC_KEY);
    if (!raw) return [];
    try {
      const doc = JSON.parse(raw) as WorldEvent[];
      if (Array.isArray(doc)) return doc.filter((e) => e && typeof e === "object");
    } catch {
      // Corrupt log: start fresh rather than serving bad state.
    }
    return [];
  }

  async record(input: {
    type: string;
    title: string;
    detail?: string;
    actorId?: string | null;
    city?: string | null;
  }): Promise<WorldEvent> {
    const event: WorldEvent = {
      id: randomId(),
      type: input.type,
      title: input.title,
      detail: input.detail ?? "",
      actorId: input.actorId ?? null,
      city: input.city ?? null,
      createdAtMs: this.now(),
    };
    const all = await this.readAll();
    all.push(event);
    const trimmed = all.length > MAX_EVENTS ? all.slice(all.length - MAX_EVENTS) : all;
    await this.kv.set(DOC_KEY, JSON.stringify(trimmed));
    return event;
  }

  async list(opts: { type?: string; limit?: number } = {}): Promise<WorldEvent[]> {
    const all = await this.readAll();
    const filtered = opts.type ? all.filter((e) => e.type === opts.type) : all;
    const limit = Math.min(Math.max(opts.limit ?? 50, 1), MAX_LIST);
    return filtered.slice(-limit).reverse();
  }
}
