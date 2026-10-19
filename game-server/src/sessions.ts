// sessions.ts — opaque bearer tokens stored in Redis with a TTL.
//
// Phase 1 dev auth: a player proves identity by id and receives an opaque
// token. Real providers (OAuth/passkey, refresh rotation — design 05 §1) replace
// `create` without touching the request pipeline, which only ever calls
// `resolve`.

import { randomBytes } from "node:crypto";
import type { KvStore } from "./kv";

export interface IssuedSession {
  token: string;
  expiresInSec: number;
}

export class SessionStore {
  constructor(
    private readonly kv: KvStore,
    private readonly ttlSec: number,
  ) {}

  static tokenKey(token: string): string {
    return `session:${token}`;
  }

  async create(playerId: string): Promise<IssuedSession> {
    const token = randomBytes(32).toString("base64url");
    await this.kv.set(SessionStore.tokenKey(token), playerId, this.ttlSec);
    return { token, expiresInSec: this.ttlSec };
  }

  async resolve(token: string): Promise<string | null> {
    if (!token || token.length > 512) return null;
    return this.kv.get(SessionStore.tokenKey(token));
  }

  async destroy(token: string): Promise<void> {
    await this.kv.del(SessionStore.tokenKey(token));
  }

  /** Refresh the TTL on use (sliding sessions). */
  async touch(token: string): Promise<void> {
    await this.kv.expire(SessionStore.tokenKey(token), this.ttlSec);
  }
}
