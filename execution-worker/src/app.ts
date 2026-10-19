/**
 * HTTP layer: Bun.serve-compatible fetch handler.
 *
 *  GET  /health    -> { ok: true } (no auth; for container healthchecks)
 *  GET  /runtimes  -> runtime registry JSON
 *  POST /execute   -> run a submission in a fresh container
 *  POST /assess    -> run a submission against public + hidden tests
 *
 * Auth: x-api-key header (or Authorization: Bearer). Quota: one token per
 * request from a per-key token bucket. Concurrency: semaphore around every
 * container run; /assess runs its tests sequentially.
 */

import { randomUUID } from "node:crypto";
import { getRuntime, type RuntimeDef } from "./runtimes";
import {
  RunnerError,
  type ContainerRunner,
  type RunOptions,
} from "./runner";
import {
  pickEntry,
  validateAssessBody,
  validateExecuteBody,
  ValidationError,
} from "./validate";
import { outputsEqual, normalizeOutput } from "./compare";
import { TokenBucket } from "./quota";
import { Semaphore } from "./semaphore";
import { DependencyError, DependencyPolicy } from "./deps";
import {
  hashFiles,
  InMemorySubmissionStore,
  type Submission,
  type SubmissionStore,
} from "./submissions";
import { AbuseDetector, type AbuseSignal } from "./abuse";
import { NullEvents, type EventSink } from "./history";

export interface AppDeps {
  apiKey: string;
  runtimes: RuntimeDef[];
  runner: ContainerRunner;
  quotaPerHour: number;
  maxConcurrent: number;
  maxRequestBytes: number;
  /** Approved-dependency policy. Defaults to an empty policy (nothing approved). */
  deps?: DependencyPolicy;
  /** Submission/revision store. Defaults to an in-memory store. */
  submissions?: SubmissionStore;
  /** Abuse signal detector. Defaults to the built-in thresholds. */
  abuse?: AbuseDetector;
  /** ClickHouse event sink for grading + moderation. Defaults to a no-op sink. */
  events?: EventSink;
}

export interface TestResult {
  name: string;
  passed: boolean;
  hidden: boolean;
  /** Only for non-hidden tests: the program's (normalized) stdout. */
  actual?: string;
  /**
   * Machine-readable failure kind. Safe for hidden tests: carries no data.
   *  - "wrong-output"  program ran, output did not match
   *  - "runtime-error"  non-zero exit before producing comparable output
   *  - "timeout"       wall-clock timeout
   *  - "build-failed"  compilation failed
   */
  errorKind?: "wrong-output" | "runtime-error" | "timeout" | "build-failed";
  /** Human-readable diagnostics. Only for non-hidden tests. */
  error?: string;
}

const ACQUIRE_TIMEOUT_MS = 30_000;

function json(data: unknown, status = 200, headers: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "content-type": "application/json", ...headers },
  });
}

function err(status: number, message: string, extra: Record<string, unknown> = {}): Response {
  return json({ error: message, ...extra }, status);
}

function checkAuth(req: Request, apiKey: string): boolean {
  const header = req.headers.get("x-api-key");
  if (header && header === apiKey) return true;
  const auth = req.headers.get("authorization");
  if (auth && auth.toLowerCase().startsWith("bearer ")) {
    return auth.slice(7).trim() === apiKey;
  }
  return false;
}

function runtimeEnv(id: string): Record<string, string> {
  // Toolchains that need writable homes/caches redirected into /work
  // because the container root filesystem is read-only.
  if (id === "go") {
    return { GOCACHE: "/work/.gocache", GOMODCACHE: "/work/.gomodcache", HOME: "/work", GOFLAGS: "-mod=mod" };
  }
  return {};
}

function classifyFailure(timedOut: boolean, exitCode: number, buildFailed: boolean): TestResult["errorKind"] {
  if (timedOut) return "timeout";
  if (buildFailed) return "build-failed";
  if (exitCode !== 0) return "runtime-error";
  return "wrong-output";
}

export function createApp(deps: AppDeps): { fetch: (req: Request) => Promise<Response> } {
  const quota = new TokenBucket(deps.quotaPerHour, deps.quotaPerHour);
  const semaphore = new Semaphore(deps.maxConcurrent);
  const depPolicy = deps.deps ?? new DependencyPolicy([]);
  const submissions = deps.submissions ?? new InMemorySubmissionStore();
  const abuse = deps.abuse ?? new AbuseDetector();
  const events = deps.events ?? new NullEvents();

  /** Resolve approved dependencies, or return an error Response. */
  function checkDependencies(runtimeId: string, requested: string[]): Response | null {
    try {
      depPolicy.resolve(runtimeId, requested);
      return null;
    } catch (e) {
      if (e instanceof DependencyError) return err(e.status, e.message);
      throw e;
    }
  }

  const keyOf = (req: Request): string => req.headers.get("x-api-key") || "bearer";

  async function runOne(
    runtime: RuntimeDef,
    entry: string,
    files: { path: string; content: string }[],
    stdin: string,
    timeoutSec: number,
  ): Promise<Awaited<ReturnType<ContainerRunner["run"]>>> {
    const acquired = await semaphore.acquire(ACQUIRE_TIMEOUT_MS);
    if (!acquired) throw new ValidationError(503, "server busy: too many concurrent executions");
    try {
      return await deps.runner.run({
        image: runtime.image,
        files,
        entry,
        buildCommand: runtime.buildCommand,
        runCommand: runtime.runCommand,
        stdin,
        timeoutSec,
        memory: runtime.memory,
        cpus: runtime.cpus,
        pidsLimit: runtime.pidsLimit,
        network: runtime.network,
        env: runtimeEnv(runtime.id),
      } satisfies RunOptions);
    } finally {
      semaphore.release();
    }
  }

  async function handleExecute(req: Request): Promise<Response> {
    const length = req.headers.get("content-length");
    if (length && Number(length) > deps.maxRequestBytes) {
      return err(413, "request body too large");
    }
    const body = await req.json().catch(() => null);
    let input;
    try {
      input = validateExecuteBody(body, deps.maxRequestBytes);
    } catch (e) {
      if (e instanceof ValidationError) return err(e.status, e.message);
      throw e;
    }
    const runtime = getRuntime(input.runtimeId)!;
    const timeoutSec = input.timeoutSec || runtime.timeoutSec;
    const entry = pickEntry(input.files, runtime.entryFile);
    const depError = checkDependencies(input.runtimeId, input.dependencies);
    if (depError) return depError;
    let revision: number | undefined;
    if (input.submissionId) {
      const sub = await submissions.addRevision(input.submissionId, input.files);
      if (!sub) return err(404, "unknown submission");
      revision = sub.revisions.length;
    }
    let result;
    try {
      result = await runOne(runtime, entry, input.files, input.stdin, timeoutSec);
    } catch (e) {
      if (e instanceof ValidationError) return err(e.status, e.message);
      if (e instanceof RunnerError) return err(502, "execution backend unavailable");
      throw e;
    }
    return json({
      runtime: runtime.id,
      stdout: result.stdout,
      stderr: result.stderr,
      exitCode: result.exitCode,
      timedOut: result.timedOut,
      truncated: result.truncated,
      durationMs: result.durationMs,
      artifacts: result.artifacts,
      revision,
    });
  }

  async function handleAssess(req: Request): Promise<Response> {
    const length = req.headers.get("content-length");
    if (length && Number(length) > deps.maxRequestBytes) {
      return err(413, "request body too large");
    }
    const body = await req.json().catch(() => null);
    let input;
    try {
      input = validateAssessBody(body, deps.maxRequestBytes);
    } catch (e) {
      if (e instanceof ValidationError) return err(e.status, e.message);
      throw e;
    }
    const runtime = getRuntime(input.runtimeId)!;
    const timeoutSec = input.timeoutSec || runtime.timeoutSec;
    const entry = pickEntry(input.files, runtime.entryFile);
    const depError = checkDependencies(input.runtimeId, input.dependencies);
    if (depError) return depError;
    let revision: number | undefined;
    if (input.submissionId) {
      const sub = await submissions.addRevision(input.submissionId, input.files);
      if (!sub) return err(404, "unknown submission");
      revision = sub.revisions.length;
    }

    const results: TestResult[] = [];
    let passed = 0;
    const startedAt = Date.now();
    for (const test of input.tests) {
      let run;
      try {
        run = await runOne(runtime, entry, input.files, test.stdin, timeoutSec);
      } catch (e) {
        if (e instanceof ValidationError) return err(e.status, e.message);
        if (e instanceof RunnerError) return err(502, "execution backend unavailable");
        throw e;
      }

      // Heuristic: if a build step exists and the run failed without
      // producing output, treat it as a build failure for diagnostics.
      const buildFailed =
        runtime.buildCommand !== null &&
        run.exitCode !== 0 &&
        normalizeOutput(run.stdout) === "";

      const ok = !run.timedOut && run.exitCode === 0 && outputsEqual(run.stdout, test.expectedStdout);
      if (ok) passed += 1;

      const base: TestResult = { name: test.name, passed: ok, hidden: test.hidden };
      if (test.hidden) {
        // NEVER leak expected/actual data for hidden tests.
        if (!ok) base.errorKind = classifyFailure(run.timedOut, run.exitCode, buildFailed);
      } else {
        base.actual = normalizeOutput(run.stdout);
        if (!ok) {
          base.errorKind = classifyFailure(run.timedOut, run.exitCode, buildFailed);
          const diag = run.stderr.trim().slice(0, 2000);
          base.error = buildFailed
            ? `build failed (exit ${run.exitCode})${diag ? `: ${diag}` : ""}`
            : run.timedOut
              ? `timed out after ${timeoutSec}s`
              : run.exitCode !== 0
                ? `runtime error (exit ${run.exitCode})${diag ? `: ${diag}` : ""}`
                : "output did not match expected";
        }
      }
      results.push(base);
    }

    const hiddenTotal = input.tests.filter((t) => t.hidden).length;
    if (revision !== undefined && input.submissionId) {
      await submissions.setVerdict(input.submissionId, revision, { passed, total: input.tests.length });
    }
    const key = keyOf(req);
    const signals: AbuseSignal[] = [
      abuse.recordSubmission(key, hashFiles(input.files)),
      abuse.recordVerdict(key, passed, input.tests.length, hiddenTotal),
    ].filter((s): s is AbuseSignal => s !== null);

    // Storage is best-effort: grading must never fail because a sink is down.
    void events
      .recordAssessment({
        requestId: randomUUID(),
        runtime: runtime.id,
        passed,
        total: input.tests.length,
        durationMs: Date.now() - startedAt,
        timedOut: results.some((r) => r.errorKind === "timeout"),
        errorKind: results.find((r) => !r.passed)?.errorKind ?? "",
      })
      .catch(() => {});
    if (signals.length > 0) {
      void events
        .recordSignals(signals.map((s) => ({ playerId: s.key, kind: s.kind, detail: s.detail, count: s.count })))
        .catch(() => {});
    }

    return json({
      runtime: runtime.id,
      passed,
      total: input.tests.length,
      revision,
      results,
    });
  }

  async function fetch(req: Request): Promise<Response> {
    const url = new URL(req.url);

    if (url.pathname === "/health" && req.method === "GET") {
      return json({ ok: true });
    }

    if (!checkAuth(req, deps.apiKey)) {
      return err(401, "unauthorized: missing or invalid x-api-key");
    }

    if (url.pathname === "/runtimes" && req.method === "GET") {
      return json({ runtimes: deps.runtimes });
    }

    if (url.pathname === "/dependencies" && req.method === "GET") {
      const runtime = url.searchParams.get("runtime") ?? undefined;
      return json({ approved: depPolicy.list(runtime) });
    }

    if (url.pathname === "/submissions" && req.method === "POST") {
      const body = await req.json().catch(() => null);
      let input;
      try {
        input = validateExecuteBody(body, deps.maxRequestBytes);
      } catch (e) {
        if (e instanceof ValidationError) return err(e.status, e.message);
        throw e;
      }
      // Storing a submission never runs code, but the dependency list must be valid.
      const depError = checkDependencies(input.runtimeId, input.dependencies);
      if (depError) return depError;
      const sub = await submissions.create(input.runtimeId, input.dependencies, input.files);
      return json(submissionMeta(sub));
    }

    if (url.pathname.startsWith("/submissions/") && req.method === "GET") {
      return await handleGetSubmission(url);
    }

    if (url.pathname === "/moderation/signals" && req.method === "GET") {
      const limit = Number(url.searchParams.get("limit") ?? "50");
      return json({ signals: abuse.signals(Number.isFinite(limit) && limit > 0 ? limit : 50) });
    }

    // Quota: one token per execution/assessment request.
    if (
      (url.pathname === "/execute" || url.pathname === "/assess") &&
      req.method === "POST"
    ) {
      const key = keyOf(req);
      const decision = quota.take(key);
      if (!decision.allowed) {
        return json(
          { error: "quota exceeded", retryAfterSec: decision.retryAfterSec },
          429,
          { "retry-after": String(decision.retryAfterSec) },
        );
      }
      const reqSignals = abuse.recordRequest(key, Number(req.headers.get("content-length") ?? "0") || 0);
      if (reqSignals.length > 0) {
        void events
          .recordSignals(reqSignals.map((s) => ({ playerId: s.key, kind: s.kind, detail: s.detail, count: s.count })))
          .catch(() => {});
      }
      const res = url.pathname === "/execute" ? await handleExecute(req) : await handleAssess(req);
      if (res.status === 429) return res;
      return res;
    }

    return err(404, "not found");
  }

  function submissionMeta(sub: Submission) {
    return {
      id: sub.id,
      runtime: sub.runtime,
      dependencies: sub.dependencies,
      created: sub.created,
      revisions: sub.revisions.map((r) => ({
        n: r.n,
        at: r.at,
        hash: r.hash,
        files: r.files.map((f) => f.path),
        verdict: r.verdict ?? null,
      })),
    };
  }

  async function handleGetSubmission(url: URL): Promise<Response> {
    const rest = url.pathname.slice("/submissions/".length);
    const slash = rest.indexOf("/");
    const id = slash === -1 ? rest : rest.slice(0, slash);
    const sub = await submissions.get(id);
    if (!sub) return err(404, "unknown submission");
    if (slash === -1) return json(submissionMeta(sub));
    const tail = rest.slice(slash + 1);
    if (tail === "compare") {
      const from = Number(url.searchParams.get("from") ?? "1");
      const to = Number(url.searchParams.get("to") ?? String(sub.revisions.length));
      if (!Number.isInteger(from) || !Number.isInteger(to)) {
        return err(400, "from and to must be revision numbers");
      }
      const diff = await submissions.compare(id, from, to);
      if (!diff) return err(404, "unknown revision");
      return json(diff);
    }
    const m = /^revisions\/(\d+)$/.exec(tail);
    if (m) {
      const rev = sub.revisions.find((r) => r.n === Number(m[1]));
      if (!rev) return err(404, "unknown revision");
      return json(rev);
    }
    return err(404, "not found");
  }

  return { fetch };
}
