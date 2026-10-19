/**
 * Submission store: kept revisions + revision comparison (design 3.4).
 *
 * A submission is a project (runtime + files) that succeeds across revisions.
 * Each revision is content-hashed and optionally carries the verdict of the
 * assessment that produced it. `compare` diffs two revisions line by line and
 * reports the verdict delta, so a player (and moderation) can see exactly what
 * changed between two attempts and whether it helped.
 *
 * Pure logic except for `node:crypto` hashing; the store is an interface so the
 * RocksDB (Kvrocks) implementation — or a future object-storage one — can
 * replace the in-memory one without touching the HTTP layer.
 */

import { createHash, randomUUID } from "node:crypto";
import type { FileInput } from "./validate";

export interface DiffLine {
  kind: "add" | "del" | "ctx";
  text: string;
}

export interface FileDiff {
  path: string;
  status: "added" | "removed" | "changed" | "unchanged";
  added: number;
  removed: number;
  lines: DiffLine[];
}

export interface Verdict {
  passed: number;
  total: number;
}

export interface Revision {
  n: number;
  at: string;
  files: FileInput[];
  hash: string;
  verdict?: Verdict;
}

export interface Submission {
  id: string;
  runtime: string;
  dependencies: string[];
  created: string;
  revisions: Revision[];
}

export interface RevisionDiff {
  from: number;
  to: number;
  files: FileDiff[];
  added: number;
  removed: number;
  verdict: { before: Verdict | null; after: Verdict | null; deltaPassed: number };
}

/** Max submissions held in memory before the oldest is evicted. */
export const MAX_STORED_SUBMISSIONS = 500;
/** Lines per file above which the diff falls back to a coarse replace. */
export const DIFF_MAX_LINES = 1200;

/** Stable content hash: independent of file order, includes the path. */
export function hashFiles(files: FileInput[]): string {
  const h = createHash("sha256");
  for (const f of [...files].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0))) {
    h.update(f.path);
    h.update("\0");
    h.update(f.content);
    h.update("\n");
  }
  return h.digest("hex").slice(0, 16);
}

/** Line-level diff via longest-common-subsequence (prefix/suffix trimmed first). */
export function diffLines(a: string, b: string, maxLines: number = DIFF_MAX_LINES): DiffLine[] {
  const A = a.length ? a.split("\n") : [];
  const B = b.length ? b.split("\n") : [];
  let pre = 0;
  const min = Math.min(A.length, B.length);
  while (pre < min && A[pre] === B[pre]) pre++;
  let endA = A.length;
  let endB = B.length;
  while (endA > pre && endB > pre && A[endA - 1] === B[endB - 1]) {
    endA--;
    endB--;
  }
  const midA = A.slice(pre, endA);
  const midB = B.slice(pre, endB);
  const out: DiffLine[] = [];
  for (let i = 0; i < pre; i++) out.push({ kind: "ctx", text: A[i] });
  if (midA.length > maxLines || midB.length > maxLines) {
    for (const t of midA) out.push({ kind: "del", text: t });
    for (const t of midB) out.push({ kind: "add", text: t });
  } else {
    out.push(...lcsDiff(midA, midB));
  }
  for (let i = endA; i < A.length; i++) out.push({ kind: "ctx", text: A[i] });
  return out;
}

function lcsDiff(A: string[], B: string[]): DiffLine[] {
  const n = A.length;
  const m = B.length;
  if (n === 0 && m === 0) return [];
  const w = m + 1;
  const dp = new Uint32Array((n + 1) * w);
  for (let i = n - 1; i >= 0; i--) {
    for (let j = m - 1; j >= 0; j--) {
      dp[i * w + j] =
        A[i] === B[j]
          ? dp[(i + 1) * w + (j + 1)] + 1
          : Math.max(dp[(i + 1) * w + j], dp[i * w + (j + 1)]);
    }
  }
  const out: DiffLine[] = [];
  let i = 0;
  let j = 0;
  while (i < n && j < m) {
    if (A[i] === B[j]) {
      out.push({ kind: "ctx", text: A[i] });
      i++;
      j++;
    } else if (dp[(i + 1) * w + j] >= dp[i * w + (j + 1)]) {
      out.push({ kind: "del", text: A[i] });
      i++;
    } else {
      out.push({ kind: "add", text: B[j] });
      j++;
    }
  }
  while (i < n) out.push({ kind: "del", text: A[i++] });
  while (j < m) out.push({ kind: "add", text: B[j++] });
  return out;
}

function countLines(lines: DiffLine[]): { added: number; removed: number } {
  let added = 0;
  let removed = 0;
  for (const l of lines) {
    if (l.kind === "add") added++;
    else if (l.kind === "del") removed++;
  }
  return { added, removed };
}

function filesByPath(files: FileInput[]): Map<string, FileInput> {
  const m = new Map<string, FileInput>();
  for (const f of files) m.set(f.path, f);
  return m;
}

/** Diff every file between two revisions; paths are matched, not ordered. */
export function compareRevisions(a: Revision, b: Revision): RevisionDiff {
  const before = filesByPath(a.files);
  const after = filesByPath(b.files);
  const paths = [...new Set([...before.keys(), ...after.keys()])].sort();
  const files: FileDiff[] = [];
  let added = 0;
  let removed = 0;
  for (const path of paths) {
    const fa = before.get(path);
    const fb = after.get(path);
    let fd: FileDiff;
    if (!fa) {
      const lines = fb!.content.length ? fb!.content.split("\n").map((text) => ({ kind: "add" as const, text })) : [];
      fd = { path, status: "added", added: lines.length, removed: 0, lines };
    } else if (!fb) {
      const lines = fa.content.length ? fa.content.split("\n").map((text) => ({ kind: "del" as const, text })) : [];
      fd = { path, status: "removed", added: 0, removed: lines.length, lines };
    } else if (fa.content === fb.content) {
      fd = { path, status: "unchanged", added: 0, removed: 0, lines: [] };
    } else {
      const lines = diffLines(fa.content, fb.content);
      const { added: da, removed: dr } = countLines(lines);
      fd = { path, status: "changed", added: da, removed: dr, lines };
    }
    added += fd.added;
    removed += fd.removed;
    files.push(fd);
  }
  return {
    from: a.n,
    to: b.n,
    files,
    added,
    removed,
    verdict: {
      before: a.verdict ?? null,
      after: b.verdict ?? null,
      deltaPassed: (b.verdict?.passed ?? 0) - (a.verdict?.passed ?? 0),
    },
  };
}

/** Stores may be in-memory (sync) or networked (async); callers always await. */
export type MaybePromise<T> = T | Promise<T>;

export interface SubmissionStore {
  create(runtime: string, dependencies: string[], files: FileInput[], at?: string): MaybePromise<Submission>;
  addRevision(id: string, files: FileInput[], at?: string): MaybePromise<Submission | undefined>;
  setVerdict(id: string, n: number, verdict: Verdict): MaybePromise<boolean>;
  get(id: string): MaybePromise<Submission | undefined>;
  list(limit?: number): MaybePromise<Submission[]>;
  compare(id: string, from: number, to: number): MaybePromise<RevisionDiff | undefined>;
}

export class InMemorySubmissionStore implements SubmissionStore {
  private readonly byId = new Map<string, Submission>();

  create(runtime: string, dependencies: string[], files: FileInput[], at: string = new Date().toISOString()): Submission {
    const sub: Submission = {
      id: randomUUID(),
      runtime,
      dependencies: [...dependencies],
      created: at,
      revisions: [{ n: 1, at, files: files.map((f) => ({ ...f })), hash: hashFiles(files) }],
    };
    this.byId.set(sub.id, sub);
    this.evict();
    return sub;
  }

  addRevision(id: string, files: FileInput[], at: string = new Date().toISOString()): Submission | undefined {
    const sub = this.byId.get(id);
    if (!sub) return undefined;
    const n = sub.revisions.length + 1;
    sub.revisions.push({ n, at, files: files.map((f) => ({ ...f })), hash: hashFiles(files) });
    return sub;
  }

  setVerdict(id: string, n: number, verdict: Verdict): boolean {
    const sub = this.byId.get(id);
    const rev = sub?.revisions.find((r) => r.n === n);
    if (!rev) return false;
    rev.verdict = { passed: verdict.passed, total: verdict.total };
    return true;
  }

  get(id: string): Submission | undefined {
    return this.byId.get(id);
  }

  list(limit = 50): Submission[] {
    return [...this.byId.values()]
      .sort((a, b) => (a.created < b.created ? 1 : -1))
      .slice(0, Math.max(0, limit));
  }

  compare(id: string, from: number, to: number): RevisionDiff | undefined {
    const sub = this.byId.get(id);
    if (!sub) return undefined;
    const a = sub.revisions.find((r) => r.n === from);
    const b = sub.revisions.find((r) => r.n === to);
    if (!a || !b) return undefined;
    return compareRevisions(a, b);
  }

  private evict(): void {
    if (this.byId.size <= MAX_STORED_SUBMISSIONS) return;
    const oldest = [...this.byId.values()].sort((a, b) => (a.created < b.created ? -1 : 1))[0];
    this.byId.delete(oldest.id);
  }
}
