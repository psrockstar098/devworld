/**
 * RocksDB-backed submission store (via Kvrocks).
 *
 * Same contract as the in-memory store, but durable and shared: a submission is
 * one JSON document per id, plus a sorted-set recency index for `list`. Reads
 * are read-modify-write, which is fine for this workload (a submission has one
 * writer in practice — the player's own revision stream); the multi-writer
 * caveat is noted in docs/design/05-system-architecture.md.
 */

import { randomUUID } from "node:crypto";
import type { FileInput } from "./validate";
import type { Kv } from "./kv";
import {
  compareRevisions,
  hashFiles,
  MAX_STORED_SUBMISSIONS,
  type RevisionDiff,
  type Submission,
  type SubmissionStore,
  type Verdict,
} from "./submissions";

const doc = (id: string) => `submission:${id}`;
const INDEX = "submissions:created";

export class KvSubmissionStore implements SubmissionStore {
  constructor(
    private readonly kv: Kv,
    private readonly max: number = MAX_STORED_SUBMISSIONS,
  ) {}

  async create(
    runtime: string,
    dependencies: string[],
    files: FileInput[],
    at: string = new Date().toISOString(),
  ): Promise<Submission> {
    const sub: Submission = {
      id: randomUUID(),
      runtime,
      dependencies: [...dependencies],
      created: at,
      revisions: [{ n: 1, at, files: clone(files), hash: hashFiles(files) }],
    };
    await this.put(sub);
    await this.kv.zadd(INDEX, Date.parse(at) || Date.now(), sub.id);
    // Keep only the newest `max` submissions. Grab the ones about to fall off
    // the index first, then drop their documents so nothing is orphaned.
    const evicted = await this.kv.zrevrange(INDEX, this.max, -1);
    if (evicted.length > 0) {
      await this.kv.zremrangebyrank(INDEX, 0, -(this.max + 1));
      await Promise.all(evicted.map((id) => this.kv.del(doc(id))));
    }
    return sub;
  }

  async addRevision(
    id: string,
    files: FileInput[],
    at: string = new Date().toISOString(),
  ): Promise<Submission | undefined> {
    const sub = await this.get(id);
    if (!sub) return undefined;
    sub.revisions.push({ n: sub.revisions.length + 1, at, files: clone(files), hash: hashFiles(files) });
    await this.put(sub);
    return sub;
  }

  async setVerdict(id: string, n: number, verdict: Verdict): Promise<boolean> {
    const sub = await this.get(id);
    const rev = sub?.revisions.find((r) => r.n === n);
    if (!sub || !rev) return false;
    rev.verdict = { passed: verdict.passed, total: verdict.total };
    await this.put(sub);
    return true;
  }

  async get(id: string): Promise<Submission | undefined> {
    const raw = await this.kv.get(doc(id));
    if (!raw) return undefined;
    try {
      return JSON.parse(raw) as Submission;
    } catch {
      return undefined;
    }
  }

  async list(limit = 50): Promise<Submission[]> {
    if (limit <= 0) return [];
    const ids = await this.kv.zrevrange(INDEX, 0, limit - 1);
    const raws = await this.kv.mget(ids.map(doc));
    return raws.filter((r): r is string => r !== null).flatMap((r) => {
      try {
        return [JSON.parse(r) as Submission];
      } catch {
        return [];
      }
    });
  }

  async compare(id: string, from: number, to: number): Promise<RevisionDiff | undefined> {
    const sub = await this.get(id);
    if (!sub) return undefined;
    const a = sub.revisions.find((r) => r.n === from);
    const b = sub.revisions.find((r) => r.n === to);
    if (!a || !b) return undefined;
    return compareRevisions(a, b);
  }

  private async put(sub: Submission): Promise<void> {
    await this.kv.set(doc(sub.id), JSON.stringify(sub));
  }
}

function clone(files: FileInput[]): FileInput[] {
  return files.map((f) => ({ ...f }));
}
