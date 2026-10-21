// auth.ts — credential auth for the game server.
//
// The old login took just a player id and handed out a token: anyone could
// impersonate any player. Login now requires the Argon2id password set at
// registration. Hashes live under their own key, never inside the player
// record, so no player-facing response can ever leak one.
//
// Registration also claims passwordless accounts created via POST /players:
// such an account holds nothing (progress requires a login, and login
// requires a password), so claiming one grants an empty account, never
// someone else's progress.

import type { KvStore } from "./kv";

export class AuthError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "AuthError";
    this.status = status;
  }
}

export function validatePassword(password: unknown): string {
  if (typeof password !== "string" || password.length < 8 || password.length > 128) {
    throw new AuthError(400, "password must be 8-128 chars");
  }
  return password;
}

export class AuthStore {
  constructor(private readonly kv: KvStore) {}

  static key(playerId: string): string {
    return `playerpass:${playerId}`;
  }

  async hasPassword(playerId: string): Promise<boolean> {
    return (await this.kv.get(AuthStore.key(playerId))) !== null;
  }

  async setPassword(playerId: string, password: string): Promise<void> {
    const hash = await Bun.password.hash(password, { algorithm: "argon2id" });
    await this.kv.set(AuthStore.key(playerId), hash);
  }

  async verify(playerId: string, password: string): Promise<boolean> {
    const hash = await this.kv.get(AuthStore.key(playerId));
    if (!hash) return false;
    try {
      return await Bun.password.verify(password, hash);
    } catch {
      return false;
    }
  }
}
