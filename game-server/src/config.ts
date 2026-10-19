// config.ts — environment configuration for the game server.
//
// Every value has a local-development default so `bun server.ts` works against
// `infra/` with no setup. Production overrides all of them from the environment.

export interface ClickHouseConfig {
  url: string;
  user: string;
  password: string;
  database: string;
}

export interface WorkerConfig {
  url: string;
  apiKey: string;
}

export interface ServerConfig {
  port: number;
  clickhouse: ClickHouseConfig;
  /** Redis-protocol URL. Used for both Redis (cache) and Kvrocks (RocksDB). */
  redisUrl: string;
  kvUrl: string;
  /** Sandbox worker for grading. When null, mission submission returns 503. */
  worker: WorkerConfig | null;
  /** Directory holding missions.json. */
  contentDir: string;
  sessionTtlSec: number;
  balanceCacheTtlSec: number;
}

function num(v: string | undefined, fallback: number): number {
  const n = Number(v);
  return Number.isFinite(n) && n > 0 ? Math.floor(n) : fallback;
}

export function loadConfig(env: Record<string, string | undefined> = process.env): ServerConfig {
  const workerUrl = env.WORKER_URL?.trim();
  return {
    port: num(env.PORT, 8091),
    clickhouse: {
      url: env.CLICKHOUSE_URL ?? "http://127.0.0.1:8123",
      user: env.CLICKHOUSE_USER ?? "devworld",
      password: env.CLICKHOUSE_PASSWORD ?? "devworld_dev_pw",
      database: env.CLICKHOUSE_DB ?? "devworld",
    },
    redisUrl:
      env.REDIS_URL ??
      `redis://:${env.REDIS_PASSWORD ?? "devworld_dev_pw"}@127.0.0.1:${env.REDIS_PORT ?? "6379"}`,
    kvUrl:
      env.KV_URL ??
      `redis://:${env.KV_PASSWORD ?? "devworld_dev_pw"}@127.0.0.1:${env.KV_PORT ?? "6666"}`,
    worker: workerUrl ? { url: workerUrl, apiKey: env.WORKER_API_KEY ?? "" } : null,
    contentDir: env.CONTENT_DIR ?? "../game-shell/content",
    sessionTtlSec: num(env.SESSION_TTL_SEC, 60 * 60 * 24 * 7),
    balanceCacheTtlSec: num(env.BALANCE_CACHE_TTL_SEC, 15),
  };
}
