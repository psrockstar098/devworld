// assess.ts — the client for the sandbox worker's grading endpoint.
//
// The game server never executes player code (design 05 §3): it forwards the
// submission and the mission's public + hidden tests to the execution worker and
// relays the verdict. Hidden test data never leaves this process except to the
// worker, and the worker's response never contains it.

import type { WorkerConfig } from "./config";

export interface AssessTest {
  name: string;
  stdin: string;
  expectedStdout: string;
  hidden: boolean;
}

export interface AssessTestResult {
  name: string;
  passed: boolean;
  hidden: boolean;
  actual?: string;
  errorKind?: string;
}

export interface AssessResult {
  passed: number;
  total: number;
  durationMs?: number;
  timedOut?: boolean;
  results: AssessTestResult[];
}

export interface AssessInput {
  runtime: string;
  files: { path: string; content: string }[];
  tests: AssessTest[];
  timeoutSec?: number;
}

export interface AssessClient {
  assess(input: AssessInput): Promise<AssessResult>;
}

/** Thrown when grading cannot run (worker down / misconfigured). */
export class AssessUnavailableError extends Error {
  readonly status = 503;
  constructor(message: string) {
    super(message);
    this.name = "AssessUnavailableError";
  }
}

export class WorkerAssessClient implements AssessClient {
  constructor(
    private readonly cfg: WorkerConfig,
    private readonly fetchImpl: typeof fetch = fetch,
  ) {}

  async assess(input: AssessInput): Promise<AssessResult> {
    const url = this.cfg.url.replace(/\/$/, "") + "/assess";
    let res: Response;
    try {
      res = await this.fetchImpl(url, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          ...(this.cfg.apiKey ? { "x-api-key": this.cfg.apiKey } : {}),
        },
        body: JSON.stringify({
          runtime: input.runtime,
          files: input.files,
          tests: input.tests.map((t) => ({
            name: t.name,
            stdin: t.stdin,
            expected_stdout: t.expectedStdout,
            hidden: t.hidden,
          })),
          ...(input.timeoutSec ? { timeout: input.timeoutSec } : {}),
        }),
      });
    } catch (e) {
      throw new AssessUnavailableError(`worker unreachable: ${(e as Error).message}`);
    }
    if (!res.ok) {
      const text = await res.text().catch(() => "");
      throw new AssessUnavailableError(`worker error ${res.status}: ${text.slice(0, 200)}`);
    }
    const data = (await res.json()) as {
      passed?: number;
      total?: number;
      durationMs?: number;
      timedOut?: boolean;
      results?: Array<{ name: string; passed: boolean; hidden: boolean; actual?: string; errorKind?: string }>;
    };
    return {
      passed: Number(data.passed ?? 0),
      total: Number(data.total ?? input.tests.length),
      durationMs: data.durationMs,
      timedOut: !!data.timedOut,
      results: (data.results ?? []).map((r) => ({
        name: r.name,
        passed: !!r.passed,
        hidden: !!r.hidden,
        // Never surface hidden detail, even if a worker misbehaves.
        actual: r.hidden ? undefined : r.actual,
        errorKind: r.errorKind,
      })),
    };
  }
}

/** Deterministic client for tests. */
export class FakeAssessClient implements AssessClient {
  calls: AssessInput[] = [];
  constructor(private readonly decide: (input: AssessInput) => AssessResult) {}

  async assess(input: AssessInput): Promise<AssessResult> {
    this.calls.push(input);
    return this.decide(input);
  }
}
