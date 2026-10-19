/**
 * Runtime registry: loads and validates runtimes.json.
 */

import { readFileSync } from "node:fs";

export interface RuntimeDef {
  id: string;
  label: string;
  version: string;
  image: string;
  /** Directory (relative to repo root) holding a Dockerfile to build `image`, or null. */
  imageBuild: string | null;
  extension: string;
  entryFile: string;
  buildCommand: string | null;
  runCommand: string;
  timeoutSec: number;
  memory: string;
  cpus: string;
  pidsLimit: number;
  network: string;
  capabilities: string[];
  notes: string;
}

const REQUIRED: Array<[keyof RuntimeDef, string]> = [
  ["id", "string"],
  ["label", "string"],
  ["version", "string"],
  ["image", "string"],
  ["extension", "string"],
  ["entryFile", "string"],
  ["runCommand", "string"],
  ["timeoutSec", "number"],
  ["memory", "string"],
  ["cpus", "string"],
  ["pidsLimit", "number"],
  ["network", "string"],
];

let cache: RuntimeDef[] | null = null;

export function loadRuntimes(jsonPath: string | URL): RuntimeDef[] {
  if (cache) return cache;
  const raw = JSON.parse(readFileSync(jsonPath, "utf8")) as {
    runtimes: unknown[];
  };
  if (!Array.isArray(raw.runtimes) || raw.runtimes.length === 0) {
    throw new Error("runtimes.json must contain a non-empty runtimes array");
  }
  const seen = new Set<string>();
  cache = raw.runtimes.map((r, i) => {
    if (typeof r !== "object" || r === null) {
      throw new Error(`runtimes[${i}]: must be an object`);
    }
    const rec = r as Record<string, unknown>;
    for (const [key, type] of REQUIRED) {
      if (typeof rec[key] !== type) {
        throw new Error(`runtimes[${i}]: ${key} must be a ${type}`);
      }
    }
    const id = rec.id as string;
    if (!/^[a-z0-9]+$/.test(id)) throw new Error(`runtimes[${i}]: id must be lowercase alphanumeric`);
    if (seen.has(id)) throw new Error(`duplicate runtime id: ${id}`);
    seen.add(id);
    if (rec.buildCommand !== null && typeof rec.buildCommand !== "string") {
      throw new Error(`runtimes[${i}]: buildCommand must be a string or null`);
    }
    if (rec.imageBuild !== null && typeof rec.imageBuild !== "string") {
      throw new Error(`runtimes[${i}]: imageBuild must be a string or null`);
    }
    if (!Array.isArray(rec.capabilities) || !rec.capabilities.every((c) => typeof c === "string")) {
      throw new Error(`runtimes[${i}]: capabilities must be a string array`);
    }
    return rec as unknown as RuntimeDef;
  });
  return cache;
}

export function getRuntime(id: string): RuntimeDef | undefined {
  return cache?.find((r) => r.id === id);
}

/** For tests: reset the module cache. */
export function __resetRuntimeCache(): void {
  cache = null;
}
