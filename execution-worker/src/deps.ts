/**
 * Approved-dependency policy (design 3.3 / 3.5).
 *
 * Player code never reaches the network, so a submission cannot `pip install`
 * or `npm install`. Instead a submission may *request* dependencies by name;
 * this policy decides whether each one is allowed:
 *
 *   - not in the allowlist          -> 403 "dependency not approved"
 *   - in the allowlist, not baked   -> 400 "approved but not available yet"
 *                                      (needs a controlled package mirror)
 *   - in the allowlist, baked       -> allowed; the runtime image has it
 *
 * The list lives in `dependencies.json` so content/ops can change it without a
 * code change. Pure: no Docker, no I/O beyond loading the JSON.
 */

import { readFileSync } from "node:fs";

export interface ApprovedDependency {
  runtime: string;
  name: string;
  /** Pinned version. Never a range: reproducible grading depends on exact pins. */
  version: string;
  /** true when the runtime image already contains it (offline). */
  baked: boolean;
}

export class DependencyError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "DependencyError";
    this.status = status;
  }
}

export class DependencyPolicy {
  private readonly approved: ApprovedDependency[];

  constructor(approved: ApprovedDependency[]) {
    this.approved = approved;
    for (const d of approved) {
      if (!d.runtime || !d.name || !d.version) {
        throw new Error("approved dependencies need runtime, name and version");
      }
      if (d.version.includes("^") || d.version.includes("~") || d.version === "*") {
        throw new Error(`dependency version must be pinned, got ${d.name}@${d.version}`);
      }
    }
  }

  static load(jsonPath: string | URL): DependencyPolicy {
    const raw = JSON.parse(readFileSync(jsonPath, "utf8")) as { approved?: unknown };
    if (!Array.isArray(raw.approved)) {
      throw new Error("dependencies.json must contain an approved array");
    }
    return new DependencyPolicy(raw.approved as ApprovedDependency[]);
  }

  /** Everything approved for a runtime, for `GET /dependencies`. */
  list(runtime?: string): ApprovedDependency[] {
    return runtime ? this.approved.filter((d) => d.runtime === runtime) : [...this.approved];
  }

  find(runtime: string, name: string): ApprovedDependency | undefined {
    return this.approved.find((d) => d.runtime === runtime && d.name === name);
  }

  /**
   * Resolve requested dependency names for a runtime.
   * Throws DependencyError on the first refusal; returns the allowed set.
   */
  resolve(runtime: string, requested: string[]): ApprovedDependency[] {
    const out: ApprovedDependency[] = [];
    const seen = new Set<string>();
    for (const name of requested) {
      if (typeof name !== "string" || name.length === 0 || name.length > 100) {
        throw new DependencyError(400, "dependency names must be 1-100 characters");
      }
      if (seen.has(name)) continue;
      seen.add(name);
      const dep = this.find(runtime, name);
      if (!dep) {
        throw new DependencyError(403, `dependency not approved for ${runtime}: ${name}`);
      }
      if (!dep.baked) {
        throw new DependencyError(
          400,
          `dependency approved but not baked into the ${runtime} image yet: ${name}@${dep.version} (needs a controlled mirror)`,
        );
      }
      out.push(dep);
    }
    return out;
  }
}

/** Max dependencies a single submission may request. */
export const MAX_REQUESTED_DEPENDENCIES = 10;
