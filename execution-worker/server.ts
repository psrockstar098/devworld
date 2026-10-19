/**
 * DEVWORLD execution worker entrypoint.
 * Reads config from the environment and starts the HTTP server.
 */

import { createApp } from "./src/app";
import { loadRuntimes } from "./src/runtimes";
import { DependencyPolicy } from "./src/deps";
import { DockerCliRunner } from "./src/runner";
import { RedisKv } from "./src/kv";
import { KvSubmissionStore } from "./src/submissions.kv";
import { ClickHouse } from "./src/clickhouse";
import { ClickHouseEvents, NullEvents, type EventSink } from "./src/history";

function requiredEnv(name: string): string {
  const v = process.env[name];
  if (!v) {
    console.error(`FATAL: ${name} is not set. Copy .env.example to .env and set it.`);
    process.exit(1);
  }
  return v;
}

function numEnv(name: string, fallback: number): number {
  const raw = process.env[name];
  if (!raw) return fallback;
  const n = Number(raw);
  if (!Number.isFinite(n) || n <= 0) {
    console.error(`FATAL: ${name} must be a positive number (got ${JSON.stringify(raw)})`);
    process.exit(1);
  }
  return n;
}

const apiKey = requiredEnv("EXEC_API_KEY");
const port = numEnv("PORT", 8090);

const runtimes = loadRuntimes(new URL("./runtimes.json", import.meta.url));
console.log(`loaded ${runtimes.length} runtimes`);

const deps = DependencyPolicy.load(new URL("./dependencies.json", import.meta.url));
console.log(`approved dependencies: ${deps.list().length}`);

// Durable submission store on the shared RocksDB (Kvrocks) service. Without
// KV_URL the worker keeps submissions in memory (single-process, non-durable).
const kvUrl = process.env.KV_URL;
const kv = kvUrl ? new RedisKv(kvUrl) : null;
const submissions = kv ? new KvSubmissionStore(kv) : undefined;
console.log(`submission store: ${submissions ? `rocksdb (${maskUrl(kvUrl!)})` : "in-memory"}`);

// ClickHouse event sink for grading + moderation. Without CLICKHOUSE_URL the
// worker runs with a no-op sink.
const chUrl = process.env.CLICKHOUSE_URL;
const events: EventSink = chUrl
  ? new ClickHouseEvents(
      new ClickHouse({
        url: chUrl,
        user: process.env.CLICKHOUSE_USER ?? "devworld",
        password: process.env.CLICKHOUSE_PASSWORD ?? "devworld_dev_pw",
        database: process.env.CLICKHOUSE_DB ?? "devworld",
      }),
    )
  : new NullEvents();
console.log(`event sink: ${chUrl ? `clickhouse (${chUrl})` : "disabled"}`);

const app = createApp({
  apiKey,
  runtimes,
  runner: new DockerCliRunner({
    submissionsDir: process.env.SUBMISSIONS_DIR ?? "/submissions",
  }),
  quotaPerHour: numEnv("EXEC_QUOTA_PER_HOUR", 60),
  maxConcurrent: Math.floor(numEnv("EXEC_MAX_CONCURRENT", 4)),
  maxRequestBytes: Math.floor(numEnv("EXEC_MAX_REQUEST_BYTES", 1024 * 1024)),
  deps,
  ...(submissions ? { submissions } : {}),
  events,
});

function maskUrl(url: string): string {
  return url.replace(/:[^:@/]*@/, ":***@");
}

const server = Bun.serve({ port, fetch: app.fetch });
console.log(`devworld execution worker listening on :${server.port}`);

async function shutdown() {
  console.log("shutting down…");
  server.stop();
  if (kv) await kv.close().catch(() => {});
  process.exit(0);
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
