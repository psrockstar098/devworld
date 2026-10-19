/**
 * Abuse detection (design 3.3: "per-player quotas, concurrency controls and
 * abuse detection").
 *
 * Quotas and the concurrency semaphore keep the service up; this module raises
 * *signals* for a moderation feed. It is deliberately pure and time-injectable
 * so the thresholds are unit-testable without sleeping.
 *
 * Signals (all advisory, never a hard block on their own):
 *  - "request-burst"        too many requests in the window
 *  - "payload-fixture"      a run that repeatedly ships oversized source
 *  - "duplicate-submission" the same content submitted many times
 *  - "hidden-tail"          repeatedly failing only hidden tests (possible
 *                           probing / brute-forcing the hidden set)
 */

export interface AbuseSignal {
  key: string;
  kind: "request-burst" | "payload-fixture" | "duplicate-submission" | "hidden-tail";
  detail: string;
  at: string;
  count: number;
}

export interface AbuseThresholds {
  windowMs: number;
  maxRequestsPerWindow: number;
  maxIdenticalSubmissions: number;
  maxPayloadBytes: number;
  payloadBytesPerWindow: number;
  maxHiddenOnlyFailures: number;
  maxSignals: number;
}

export const DEFAULT_THRESHOLDS: AbuseThresholds = {
  windowMs: 60_000,
  maxRequestsPerWindow: 60,
  maxIdenticalSubmissions: 5,
  maxPayloadBytes: 900 * 1024,
  payloadBytesPerWindow: 8 * 1024 * 1024,
  maxHiddenOnlyFailures: 8,
  maxSignals: 200,
};

interface KeyState {
  requests: number[];
  bytes: number[];
  hashes: Map<string, number>;
  hiddenOnlyFailures: number;
}

export class AbuseDetector {
  private readonly t: AbuseThresholds;
  private readonly state = new Map<string, KeyState>();
  private readonly log: AbuseSignal[] = [];
  /** Suppress repeat signals of the same kind for the same key within a window. */
  private readonly lastEmitted = new Map<string, number>();

  constructor(thresholds: Partial<AbuseThresholds> = {}) {
    this.t = { ...DEFAULT_THRESHOLDS, ...thresholds };
  }

  private st(key: string): KeyState {
    let s = this.state.get(key);
    if (!s) {
      s = { requests: [], bytes: [], hashes: new Map(), hiddenOnlyFailures: 0 };
      this.state.set(key, s);
    }
    return s;
  }

  private prune(now: number, s: KeyState): void {
    const cut = now - this.t.windowMs;
    s.requests = s.requests.filter((t) => t >= cut);
    s.bytes = s.bytes.filter((t) => t >= cut);
  }

  private emit(key: string, kind: AbuseSignal["kind"], detail: string, now: number, count: number): AbuseSignal | null {
    const dedupe = `${key}:${kind}`;
    const last = this.lastEmitted.get(dedupe) ?? -Infinity;
    if (now - last < this.t.windowMs) return null;
    this.lastEmitted.set(dedupe, now);
    const sig: AbuseSignal = { key, kind, detail, at: new Date(now).toISOString(), count };
    this.log.push(sig);
    while (this.log.length > this.t.maxSignals) this.log.shift();
    return sig;
  }

  /** Record an incoming execute/assess request; returns any new signals. */
  recordRequest(key: string, bytes: number, now: number = Date.now()): AbuseSignal[] {
    const s = this.st(key);
    this.prune(now, s);
    s.requests.push(now);
    s.bytes.push(bytes);
    const out: AbuseSignal[] = [];
    if (s.requests.length > this.t.maxRequestsPerWindow) {
      const sig = this.emit(key, "request-burst", `${s.requests.length} requests in ${this.t.windowMs}ms`, now, s.requests.length);
      if (sig) out.push(sig);
    }
    const totalBytes = s.bytes.reduce((a, b) => a + b, 0);
    if (bytes > this.t.maxPayloadBytes || totalBytes > this.t.payloadBytesPerWindow) {
      const sig = this.emit(key, "payload-fixture", `${totalBytes} bytes in ${this.t.windowMs}ms`, now, totalBytes);
      if (sig) out.push(sig);
    }
    return out;
  }

  /** Record a submission content hash; signals on identical resubmission. */
  recordSubmission(key: string, hash: string, now: number = Date.now()): AbuseSignal | null {
    const s = this.st(key);
    const n = (s.hashes.get(hash) ?? 0) + 1;
    s.hashes.set(hash, n);
    if (n > this.t.maxIdenticalSubmissions) {
      return this.emit(key, "duplicate-submission", `${n} identical submissions (${hash})`, now, n);
    }
    return null;
  }

  /** Record a verdict; signals when only hidden tests keep failing. */
  recordVerdict(key: string, passed: number, total: number, hiddenTotal: number, now: number = Date.now()): AbuseSignal | null {
    const s = this.st(key);
    const openTotal = total - hiddenTotal;
    // Hidden-only failure: every public test passes, at least one hidden test fails.
    const hiddenOnly = hiddenTotal > 0 && passed === openTotal && passed < total;
    if (hiddenOnly) {
      s.hiddenOnlyFailures += 1;
      if (s.hiddenOnlyFailures >= this.t.maxHiddenOnlyFailures) {
        return this.emit(
          key,
          "hidden-tail",
          `${s.hiddenOnlyFailures} runs failing only hidden tests`,
          now,
          s.hiddenOnlyFailures,
        );
      }
    }
    return null;
  }

  signals(limit = 50): AbuseSignal[] {
    return this.log.slice(-Math.max(0, limit));
  }
}
