// app.ts — the DEVWORLD game API.
//
// A small, dependency-light router over Bun's Request/Response. Every store is
// an injected interface, so the whole surface is unit-tested against in-memory
// doubles (tests/app.test.ts) and the real ClickHouse/Redis/Kvrocks run in
// integration tests. Handlers never execute player code: grading goes to the
// sandbox worker.

import { createHash } from "node:crypto";
import type { AssessClient } from "./assess";
import { AssessUnavailableError } from "./assess";
import type { EventSink } from "./history";
import type { KvStore } from "./kv";
import { PlayerError, PlayerStore, validatePlayerId } from "./players";
import { PresenceStore } from "./presence";
import { EventStore } from "./events";
import { FriendStore } from "./friends";
import { SessionStore } from "./sessions";
import { loadMissionCatalog, summarize, assessTestsFor, type MissionDef } from "./missions";
import { levelFromTotal, progressionView } from "./progression";
import { LedgerError, payout, validateTransaction, type LedgerEntry } from "./ledger";

export interface AppDeps {
  history: EventSink;
  /** Durable key-value (Kvrocks / RocksDB). */
  kv: KvStore;
  /** Disposable cache (Redis). */
  cache: KvStore;
  assess: AssessClient | null;
  missions: Map<string, MissionDef>;
  adminKey?: string;
  sessionTtlSec?: number;
  balanceCacheTtlSec?: number;
}

const MAX_FILES = 20;
const MAX_FILE_BYTES = 200 * 1024;
const MAX_TOTAL_BYTES = 1024 * 1024;
const PATH_RE = /^[A-Za-z0-9._/-]{1,128}$/;

export class ApiError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

interface FileInput {
  path: string;
  content: string;
}

export function validateFiles(value: unknown): FileInput[] {
  if (!Array.isArray(value) || value.length === 0) throw new ApiError(400, "files must be a non-empty array");
  if (value.length > MAX_FILES) throw new ApiError(400, `at most ${MAX_FILES} files`);
  let total = 0;
  const seen = new Set<string>();
  return value.map((f) => {
    const path = String((f as { path?: unknown })?.path ?? "");
    const content = String((f as { content?: unknown })?.content ?? "");
    if (!PATH_RE.test(path) || path.includes("..") || path.startsWith("/")) {
      throw new ApiError(400, `unsafe file path: ${path}`);
    }
    if (seen.has(path)) throw new ApiError(400, `duplicate file path: ${path}`);
    seen.add(path);
    const bytes = new TextEncoder().encode(content).length;
    if (bytes > MAX_FILE_BYTES) throw new ApiError(413, `file too large: ${path}`);
    total += bytes;
    if (total > MAX_TOTAL_BYTES) throw new ApiError(413, "submission too large");
    return { path, content };
  });
}

export function hashSubmission(files: FileInput[]): string {
  const h = createHash("sha256");
  for (const f of [...files].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0))) {
    h.update(f.path);
    h.update("\0");
    h.update(f.content);
    h.update("\n");
  }
  return h.digest("hex").slice(0, 16);
}

export function createApp(deps: AppDeps): { fetch: (req: Request) => Promise<Response> } {
  const players = new PlayerStore(deps.kv);
  const sessions = new SessionStore(deps.cache, deps.sessionTtlSec ?? 60 * 60 * 24 * 7);
  const presence = new PresenceStore(deps.cache);
  const events = new EventStore(deps.kv);
  const friends = new FriendStore(deps.kv);
  const balanceTtl = deps.balanceCacheTtlSec ?? 15;

  const json = (body: unknown, status = 200): Response =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
  const err = (status: number, message: string): Response => json({ error: message }, status);

  const tokenOf = (req: Request): string => {
    const auth = req.headers.get("authorization") ?? "";
    return auth.toLowerCase().startsWith("bearer ") ? auth.slice(7).trim() : "";
  };

  async function requirePlayer(req: Request): Promise<string> {
    const token = tokenOf(req);
    const playerId = token ? await sessions.resolve(token) : null;
    if (!playerId) throw new ApiError(401, "unauthorized");
    await sessions.touch(token);
    return playerId;
  }

  async function balanceOf(playerId: string, currency = "credits"): Promise<number> {
    const key = `balance:${playerId}:${currency}`;
    const cached = await deps.cache.get(key).catch(() => null);
    if (cached != null) return Number(cached);
    const balance = await deps.history.playerBalance(playerId, currency);
    await deps.cache.set(key, String(balance), balanceTtl).catch(() => {});
    return balance;
  }

  /** A mission as the client may see it: hidden tests carry no data. */
  function publicMission(m: MissionDef) {
    return {
      id: m.id,
      title: m.title,
      language: m.language,
      level: m.level,
      requires: m.requires,
      gate: m.gate,
      xpReward: m.xpReward,
      creditReward: m.creditReward,
      tests: m.tests.map((t) =>
        t.hidden ? { name: t.name, hidden: true } : { name: t.name, hidden: false, stdin: t.stdin, expected: t.expected },
      ),
    };
  }

  async function handleHealth(): Promise<Response> {
    const [ch, kv, cache] = await Promise.all([
      deps.history.ping().catch(() => false),
      deps.kv.ping().catch(() => false),
      deps.cache.ping().catch(() => false),
    ]);
    const ok = ch && kv && cache;
    return json({ ok, stores: { clickhouse: ch, kvrocks: kv, redis: cache }, missions: deps.missions.size }, ok ? 200 : 503);
  }

  async function readJson(req: Request): Promise<Record<string, unknown>> {
    const body = await req.json().catch(() => null);
    if (body == null || typeof body !== "object" || Array.isArray(body)) {
      throw new ApiError(400, "body must be a JSON object");
    }
    return body as Record<string, unknown>;
  }

  async function handleCreatePlayer(req: Request): Promise<Response> {
    const body = await readJson(req);
    const rec = await players.create({
      id: String(body.id ?? ""),
      handle: String(body.handle ?? ""),
      displayName: body.displayName != null ? String(body.displayName) : undefined,
    });
    await deps.history.insertEvents([{ playerId: rec.id, event: "player.created", service: "game-server" }]);
    return json({ id: rec.id, handle: rec.handle, displayName: rec.displayName, createdAt: rec.createdAt }, 201);
  }

  async function handleGetPlayer(id: string): Promise<Response> {
    const rec = await players.get(id);
    if (!rec) return err(404, "no such player");
    // Public subset only — no flags, no internal ids beyond the public handle.
    return json({
      id: rec.id,
      handle: rec.handle,
      displayName: rec.displayName,
      completedMissions: rec.completedMissions,
    });
  }

  async function handleLogin(req: Request): Promise<Response> {
    const body = await readJson(req);
    const playerId = String(body.playerId ?? "");
    if (!playerId) throw new ApiError(400, "playerId is required");
    if (!(await players.get(playerId))) return err(404, "no such player");
    const issued = await sessions.create(playerId);
    return json(issued);
  }

  async function handleLogout(req: Request): Promise<Response> {
    await requirePlayer(req);
    const token = tokenOf(req);
    await sessions.destroy(token);
    return json({ ok: true });
  }

  async function handleMe(req: Request): Promise<Response> {
    const playerId = await requirePlayer(req);
    const rec = await players.get(playerId);
    if (!rec) return err(404, "no such player");
    const [totalXp, balance] = await Promise.all([
      deps.history.playerTotalXp(playerId),
      balanceOf(playerId),
    ]);
    return json({
      id: rec.id,
      handle: rec.handle,
      displayName: rec.displayName,
      completedMissions: rec.completedMissions,
      progression: progressionView(totalXp),
      balance,
    });
  }

  async function handleListMissions(): Promise<Response> {
    const missions = [...deps.missions.values()]
      .sort((a, b) => a.id.localeCompare(b.id))
      .map(publicMission);
    return json({ missions });
  }

  async function handleSubmitMission(req: Request, missionId: string): Promise<Response> {
    const playerId = await requirePlayer(req);
    const mission = deps.missions.get(missionId);
    if (!mission) return err(404, "no such mission");
    const rec = await players.get(playerId);
    if (!rec) return err(404, "no such player");
    if (rec.completedMissions.includes(mission.id)) {
      return err(409, "mission already completed");
    }
    const missing = mission.requires.filter((r) => !rec.completedMissions.includes(r));
    if (missing.length) return err(403, `locked: finish ${missing.join(", ")} first`);
    const totalXpBefore = await deps.history.playerTotalXp(playerId);
    const level = levelFromTotal(totalXpBefore).level;
    if (level < mission.level) return err(403, `locked: requires level ${mission.level}`);

    const body = await readJson(req);
    const files = validateFiles(body.files);
    if (!deps.assess) return err(503, "grading backend not configured (set WORKER_URL)");

    let result;
    try {
      result = await deps.assess.assess({
        runtime: mission.language,
        files,
        tests: assessTestsFor(mission),
      });
    } catch (e) {
      if (e instanceof AssessUnavailableError) return err(503, e.message);
      throw e;
    }

    const grading = summarize(mission, result);
    const codeHash = hashSubmission(files);

    await deps.history.insertMissionAttempt({
      playerId,
      missionId: mission.id,
      runtime: mission.language,
      codeHash,
      passed: grading.passed,
      publicPassed: grading.publicPassed,
      hiddenPassed: grading.hiddenPassed,
      total: grading.total,
      durationMs: result.durationMs ?? 0,
    });
    await deps.history.insertEvents([
      {
        playerId,
        event: "mission.attempt",
        service: "game-server",
        props: { missionId: mission.id, passed: grading.passed, publicPassed: grading.publicPassed, hiddenPassed: grading.hiddenPassed },
      },
    ]);

    let totalXp = totalXpBefore;
    let rewarded = false;
    if (grading.passed && grading.rewards) {
      const ref = `mission:${mission.id}:${playerId}:${Date.now()}`;
      const entries: LedgerEntry[] = payout(playerId, ref, grading.rewards.credits, `reward ${mission.id}`);
      validateTransaction(entries);
      await deps.history.insertLedger(
        entries.map((e) => ({ ref: e.ref, playerId: e.playerId, account: e.account, amount: e.amount, currency: e.currency, memo: e.memo })),
      );
      totalXp = totalXpBefore + grading.rewards.xp;
      await deps.history.insertXp([
        { playerId, source: "mission", ref: mission.id, amount: grading.rewards.xp, levelAfter: levelFromTotal(totalXp).level },
      ]);
      await players.markCompleted(playerId, mission.id);
      await deps.cache.del(`balance:${playerId}:credits`).catch(() => {});
      rewarded = true;
    }

    return json({
      missionId: mission.id,
      passed: grading.passed,
      total: grading.total,
      publicPassed: grading.publicPassed,
      hiddenPassed: grading.hiddenPassed,
      results: grading.results,
      rewarded,
      progression: progressionView(totalXp),
      balance: await balanceOf(playerId),
    });
  }

  async function handleLedgerBalance(req: Request): Promise<Response> {
    const playerId = await requirePlayer(req);
    return json({ playerId, currency: "credits", balance: await balanceOf(playerId) });
  }

  async function handleAdminTransaction(req: Request): Promise<Response> {
    if (!deps.adminKey) return err(404, "not found");
    if (req.headers.get("x-admin-key") !== deps.adminKey) return err(401, "unauthorized");
    const body = await readJson(req);
    const entries = (Array.isArray(body.entries) ? body.entries : []).map((e) => {
      const r = e as Record<string, unknown>;
      return {
        ref: String(r.ref ?? ""),
        playerId: String(r.playerId ?? ""),
        account: String(r.account ?? ""),
        amount: Number(r.amount ?? 0),
        currency: r.currency != null ? String(r.currency) : undefined,
        memo: r.memo != null ? String(r.memo) : undefined,
      };
    });
    validateTransaction(entries);
    await deps.history.insertLedger(
      entries.map((e) => ({ ref: e.ref, playerId: e.playerId, account: e.account, amount: e.amount, currency: e.currency ?? "credits", memo: e.memo ?? "" })),
    );
    for (const pid of new Set(entries.map((e) => e.playerId))) {
      await deps.cache.del(`balance:${pid}:credits`).catch(() => {});
    }
    return json({ ok: true, entries: entries.length });
  }

  async function handleMissionStats(): Promise<Response> {
    return json({ missions: await deps.history.missionStats() });
  }

  const LOCATION_RE = /^[A-Za-z0-9_-]{1,64}$/;

  async function handlePresenceHeartbeat(req: Request): Promise<Response> {
    const playerId = await requirePlayer(req);
    const body = await readJson(req);
    const city = String(body.city ?? "");
    const map = body.map != null ? String(body.map) : "";
    if (!LOCATION_RE.test(city)) throw new ApiError(400, "city must be 1-64 chars [A-Za-z0-9_-]");
    if (map !== "" && !LOCATION_RE.test(map)) throw new ApiError(400, "map must be 1-64 chars [A-Za-z0-9_-]");
    const entry = await presence.heartbeat(playerId, city, map);
    return json({ ok: true, ...entry });
  }

  async function handlePresenceOnline(req: Request): Promise<Response> {
    await requirePlayer(req);
    const entries = await presence.listOnline();
    const listed = [];
    for (const e of entries) {
      const rec = await players.get(e.playerId);
      listed.push({
        playerId: e.playerId,
        handle: rec?.handle ?? null,
        displayName: rec?.displayName ?? null,
        city: e.city,
        map: e.map,
        lastSeenMs: e.lastSeenMs,
      });
    }
    return json({ players: listed });
  }

  const EVENT_TYPE_RE = /^[A-Za-z0-9_.-]{1,64}$/;

  async function handleRecordEvent(req: Request): Promise<Response> {
    if (!deps.adminKey) return err(404, "not found");
    if (req.headers.get("x-admin-key") !== deps.adminKey) return err(401, "unauthorized");
    const body = await readJson(req);
    const type = String(body.type ?? "");
    const title = String(body.title ?? "");
    const detail = body.detail != null ? String(body.detail) : "";
    const actorId = body.actorId != null ? String(body.actorId) : null;
    const city = body.city != null ? String(body.city) : null;
    if (!EVENT_TYPE_RE.test(type)) throw new ApiError(400, "type must be 1-64 chars [A-Za-z0-9_.-]");
    if (title.length < 1 || title.length > 200) throw new ApiError(400, "title must be 1-200 chars");
    if (detail.length > 2000) throw new ApiError(400, "detail must be at most 2000 chars");
    if (actorId !== null && actorId.length > 64) throw new ApiError(400, "actorId must be at most 64 chars");
    if (city !== null && city.length > 64) throw new ApiError(400, "city must be at most 64 chars");
    const event = await events.record({ type, title, detail, actorId, city });
    return json(event, 201);
  }

  async function handleListEvents(req: Request): Promise<Response> {
    await requirePlayer(req);
    const url = new URL(req.url);
    const type = url.searchParams.get("type") ?? undefined;
    const limitRaw = url.searchParams.get("limit");
    const limit = limitRaw != null ? Number(limitRaw) : undefined;
    if (limit !== undefined && (!Number.isFinite(limit) || limit < 1)) throw new ApiError(400, "limit must be a positive number");
    if (type !== undefined && !EVENT_TYPE_RE.test(type)) throw new ApiError(400, "type must be 1-64 chars [A-Za-z0-9_.-]");
    const opts: { type?: string; limit?: number } = {};
    if (type !== undefined) opts.type = type;
    if (limit !== undefined) opts.limit = limit;
    return json({ events: await events.list(opts) });
  }

  async function friendTarget(req: Request): Promise<{ me: string; other: string }> {
    const me = await requirePlayer(req);
    const body = await readJson(req);
    const other = String(body.playerId ?? "");
    validatePlayerId(other);
    if (other === me) throw new ApiError(400, "cannot friend yourself");
    if (!(await players.get(other))) throw new ApiError(404, "no such player");
    return { me, other };
  }

  async function handleFriendRequest(req: Request): Promise<Response> {
    const { me, other } = await friendTarget(req);
    const result = await friends.request(me, other);
    if (result !== "ok") return err(409, result === "already_friends" ? "already friends" : "request already pending");
    return json({ ok: true, from: me, to: other });
  }

  async function handleFriendAccept(req: Request): Promise<Response> {
    const { me, other } = await friendTarget(req);
    if (!(await friends.accept(me, other))) return err(404, "no such request");
    return json({ ok: true, friends: [me, other] });
  }

  async function handleFriendDecline(req: Request): Promise<Response> {
    const { me, other } = await friendTarget(req);
    if (!(await friends.decline(me, other))) return err(404, "no such request");
    return json({ ok: true });
  }

  async function handleListFriends(req: Request): Promise<Response> {
    const me = await requirePlayer(req);
    const ids = await friends.friendsOf(me);
    const online = new Map((await presence.listOnline()).map((e) => [e.playerId, e]));
    const list = [];
    for (const id of ids) {
      const rec = await players.get(id);
      const here = online.get(id);
      list.push({
        playerId: id,
        handle: rec?.handle ?? null,
        displayName: rec?.displayName ?? null,
        online: here !== undefined,
        city: here?.city ?? null,
      });
    }
    return json({ friends: list });
  }

  async function handleListFriendRequests(req: Request): Promise<Response> {
    const me = await requirePlayer(req);
    const ids = await friends.pendingFor(me);
    const list = [];
    for (const id of ids) {
      const rec = await players.get(id);
      list.push({ playerId: id, handle: rec?.handle ?? null, displayName: rec?.displayName ?? null });
    }
    return json({ requests: list });
  }

  async function route(req: Request): Promise<Response> {
    const url = new URL(req.url);
    const path = url.pathname;
    const method = req.method;

    if (path === "/health" && method === "GET") return handleHealth();
    if (path === "/" && method === "GET") {
      return json({ service: "devworld-game-server", version: "0.1.0", missions: deps.missions.size });
    }

    if (path === "/players" && method === "POST") return handleCreatePlayer(req);
    if (path === "/auth/login" && method === "POST") return handleLogin(req);
    if (path === "/auth/logout" && method === "POST") return handleLogout(req);

    const playerMatch = /^\/players\/([A-Za-z0-9_-]{6,64})$/.exec(path);
    if (playerMatch && method === "GET") return handleGetPlayer(playerMatch[1]);

    if (path === "/me" && method === "GET") return handleMe(req);
    if (path === "/progression/me" && method === "GET") return handleMe(req);
    if (path === "/missions" && method === "GET") return handleListMissions();
    if (path === "/stats/missions" && method === "GET") return handleMissionStats();

    const submitMatch = /^\/missions\/([a-z0-9_]+)\/submit$/.exec(path);
    if (submitMatch && method === "POST") return handleSubmitMission(req, submitMatch[1]);

    if (path === "/ledger/balance" && method === "GET") return handleLedgerBalance(req);
    if (path === "/admin/ledger" && method === "POST") return handleAdminTransaction(req);

    if (path === "/presence/heartbeat" && method === "POST") return handlePresenceHeartbeat(req);
    if (path === "/presence/online" && method === "GET") return handlePresenceOnline(req);

    if (path === "/events" && method === "POST") return handleRecordEvent(req);
    if (path === "/events" && method === "GET") return handleListEvents(req);

    if (path === "/friends/request" && method === "POST") return handleFriendRequest(req);
    if (path === "/friends/accept" && method === "POST") return handleFriendAccept(req);
    if (path === "/friends/decline" && method === "POST") return handleFriendDecline(req);
    if (path === "/friends" && method === "GET") return handleListFriends(req);
    if (path === "/friends/requests" && method === "GET") return handleListFriendRequests(req);

    return err(404, "not found");
  }

  async function fetch(req: Request): Promise<Response> {
    try {
      return await route(req);
    } catch (e) {
      if (e instanceof ApiError) return err(e.status, e.message);
      if (e instanceof PlayerError) return err(e.status, e.message);
      if (e instanceof LedgerError) return err(e.status, e.message);
      if (e instanceof AssessUnavailableError) return err(e.status, e.message);
      return err(500, "internal error");
    }
  }

  return { fetch };
}
