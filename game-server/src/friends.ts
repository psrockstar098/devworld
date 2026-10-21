// friends.ts — the real-player social graph.
//
// Friendships are between REAL player accounts only: both sides are player
// ids that exist in the player store, and a friendship only exists after
// the other side accepts. No fake friends, no seeded relationships — the
// graph starts empty and grows from what players actually do.
//
// Storage is durable (kv): friendships must survive restarts. Online
// status is NOT stored here; readers join against the presence store.

import type { KvStore } from "./kv";

const GRAPH_KEY = "friends:graph";
const REQUESTS_KEY = "friends:requests";

type IdMap = Record<string, string[]>;

async function readDoc(kv: KvStore, key: string): Promise<IdMap> {
  const raw = await kv.get(key);
  if (!raw) return {};
  try {
    const doc = JSON.parse(raw) as IdMap;
    if (doc && typeof doc === "object" && !Array.isArray(doc)) return doc;
  } catch {
    // Corrupt doc: start fresh rather than serving bad state.
  }
  return {};
}

function removeId(list: string[], id: string): string[] {
  return list.filter((x) => x !== id);
}

export class FriendStore {
  constructor(private readonly kv: KvStore) {}

  /** "ok" | "already_friends" | "already_pending". Caller checks self/unknown ids. */
  async request(fromId: string, toId: string): Promise<"ok" | "already_friends" | "already_pending"> {
    const [graph, requests] = await Promise.all([readDoc(this.kv, GRAPH_KEY), readDoc(this.kv, REQUESTS_KEY)]);
    if ((graph[fromId] ?? []).includes(toId)) return "already_friends";
    if ((requests[toId] ?? []).includes(fromId)) return "already_pending";
    requests[toId] = [...(requests[toId] ?? []), fromId];
    await this.kv.set(REQUESTS_KEY, JSON.stringify(requests));
    return "ok";
  }

  /** False when no such pending request exists. */
  async accept(toId: string, fromId: string): Promise<boolean> {
    const [graph, requests] = await Promise.all([readDoc(this.kv, GRAPH_KEY), readDoc(this.kv, REQUESTS_KEY)]);
    if (!(requests[toId] ?? []).includes(fromId)) return false;
    requests[toId] = removeId(requests[toId], fromId);
    graph[toId] = [...new Set([...(graph[toId] ?? []), fromId])];
    graph[fromId] = [...new Set([...(graph[fromId] ?? []), toId])];
    await Promise.all([
      this.kv.set(REQUESTS_KEY, JSON.stringify(requests)),
      this.kv.set(GRAPH_KEY, JSON.stringify(graph)),
    ]);
    return true;
  }

  /** False when no such pending request exists. */
  async decline(toId: string, fromId: string): Promise<boolean> {
    const requests = await readDoc(this.kv, REQUESTS_KEY);
    if (!(requests[toId] ?? []).includes(fromId)) return false;
    requests[toId] = removeId(requests[toId], fromId);
    await this.kv.set(REQUESTS_KEY, JSON.stringify(requests));
    return true;
  }

  async friendsOf(playerId: string): Promise<string[]> {
    return (await readDoc(this.kv, GRAPH_KEY))[playerId] ?? [];
  }

  async pendingFor(playerId: string): Promise<string[]> {
    return (await readDoc(this.kv, REQUESTS_KEY))[playerId] ?? [];
  }
}
