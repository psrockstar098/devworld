/**
 * DEVWORLD execution worker entrypoint.
 * Reads config from the environment and starts the HTTP server.
 */

import { createApp } from "./src/app";
import { loadRuntimes } from "./src/runtimes";
import { DockerCliRunner } from "./src/runner";

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

const app = createApp({
  apiKey,
  runtimes,
  runner: new DockerCliRunner({
    submissionsDir: process.env.SUBMISSIONS_DIR ?? "/submissions",
  }),
  quotaPerHour: numEnv("EXEC_QUOTA_PER_HOUR", 60),
  maxConcurrent: Math.floor(numEnv("EXEC_MAX_CONCURRENT", 4)),
  maxRequestBytes: Math.floor(numEnv("EXEC_MAX_REQUEST_BYTES", 1024 * 1024)),
});

Bun.serve({ port, fetch: app.fetch });
console.log(`devworld execution worker listening on :${port}`);
