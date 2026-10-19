// server.ts — DEVWORLD game API entrypoint.
//
// Wires the real stores (ClickHouse history, Kvrocks durable KV, Redis cache),
// loads the mission catalogue, and serves the app over Bun.

import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { loadConfig } from "./src/config";
import { ClickHouse } from "./src/clickhouse";
import { ClickHouseHistory } from "./src/history";
import { RedisKv } from "./src/kv";
import { WorkerAssessClient } from "./src/assess";
import { loadMissionCatalog } from "./src/missions";
import { createApp } from "./src/app";

const cfg = loadConfig();

const ch = new ClickHouse(cfg.clickhouse);
const history = new ClickHouseHistory(ch);
const kv = new RedisKv(cfg.kvUrl);
const cache = new RedisKv(cfg.redisUrl);
const assess = cfg.worker ? new WorkerAssessClient(cfg.worker) : null;

const missionsPath = resolve(import.meta.dir, cfg.contentDir, "missions.json");
const missions = loadMissionCatalog(JSON.parse(readFileSync(missionsPath, "utf8")));

const app = createApp({
  history,
  kv,
  cache,
  assess,
  missions,
  adminKey: process.env.ADMIN_KEY,
  sessionTtlSec: cfg.sessionTtlSec,
  balanceCacheTtlSec: cfg.balanceCacheTtlSec,
});

const server = Bun.serve({ port: cfg.port, fetch: app.fetch });

console.log(
  [
    `devworld-game-server listening on :${server.port}`,
    `  clickhouse  ${cfg.clickhouse.url} (db ${cfg.clickhouse.database})`,
    `  kvrocks     ${cfg.kvUrl.replace(/:[^:@/]*@/, ":***@")}`,
    `  redis       ${cfg.redisUrl.replace(/:[^:@/]*@/, ":***@")}`,
    `  worker      ${cfg.worker ? cfg.worker.url : "(not configured — grading disabled)"}`,
    `  missions    ${missions.size} (${missionsPath})`,
  ].join("\n"),
);

async function shutdown() {
  console.log("shutting down…");
  server.stop();
  await Promise.allSettled([kv.close(), cache.close()]);
  process.exit(0);
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
