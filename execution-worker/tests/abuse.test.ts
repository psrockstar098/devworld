import { describe, expect, test } from "bun:test";
import { AbuseDetector } from "../src/abuse";

describe("abuse detector", () => {
  test("flags a request burst, once per window", () => {
    const d = new AbuseDetector({ maxRequestsPerWindow: 3, windowMs: 10_000 });
    const t = 1_000_000;
    expect(d.recordRequest("k", 10, t).length).toBe(0);
    expect(d.recordRequest("k", 10, t + 1).length).toBe(0);
    expect(d.recordRequest("k", 10, t + 2).length).toBe(0);
    const burst = d.recordRequest("k", 10, t + 3);
    expect(burst.length).toBe(1);
    expect(burst[0].kind).toBe("request-burst");
    // deduped inside the same window
    expect(d.recordRequest("k", 10, t + 4).length).toBe(0);
    // a fresh window can signal again (the old timestamps are pruned)
    const later = t + 20_000;
    expect(d.recordRequest("k", 10, later).length).toBe(0);
    expect(d.recordRequest("k", 10, later + 1).length).toBe(0);
    expect(d.recordRequest("k", 10, later + 2).length).toBe(0);
    expect(d.recordRequest("k", 10, later + 3).length).toBe(1);
  });

  test("separate keys do not share a budget", () => {
    const d = new AbuseDetector({ maxRequestsPerWindow: 1, windowMs: 10_000 });
    expect(d.recordRequest("a", 1, 1000).length).toBe(0);
    expect(d.recordRequest("b", 1, 1000).length).toBe(0);
    expect(d.recordRequest("a", 1, 1001).length).toBe(1);
  });

  test("flags an oversized payload", () => {
    const d = new AbuseDetector({ maxPayloadBytes: 100, payloadBytesPerWindow: 1000 });
    expect(d.recordRequest("k", 50, 1).length).toBe(0);
    const sig = d.recordRequest("k", 200, 2);
    expect(sig.length).toBe(1);
    expect(sig[0].kind).toBe("payload-fixture");
  });

  test("flags identical resubmissions past the threshold", () => {
    const d = new AbuseDetector({ maxIdenticalSubmissions: 2 });
    expect(d.recordSubmission("k", "abc", 1)).toBeNull();
    expect(d.recordSubmission("k", "abc", 2)).toBeNull();
    const sig = d.recordSubmission("k", "abc", 3);
    expect(sig?.kind).toBe("duplicate-submission");
    expect(sig?.count).toBe(3);
    expect(d.recordSubmission("k", "other", 4)).toBeNull();
  });

  test("flags only-hidden failures, not public failures", () => {
    const d = new AbuseDetector({ maxHiddenOnlyFailures: 2 });
    // Public test failing (passed < openTotal): not a hidden-tail signal.
    expect(d.recordVerdict("k", 0, 2, 1, 1)).toBeNull();
    // Public passes, hidden fails: counted.
    expect(d.recordVerdict("k", 1, 2, 1, 2)).toBeNull();
    const sig = d.recordVerdict("k", 1, 2, 1, 3);
    expect(sig?.kind).toBe("hidden-tail");
  });

  test("keeps a bounded, newest-first-safe signal log", () => {
    const d = new AbuseDetector({ maxRequestsPerWindow: 0, windowMs: 1, maxSignals: 3 });
    for (let i = 0; i < 10; i++) d.recordRequest("k", 1, i * 1000);
    const signals = d.signals();
    expect(signals.length).toBeLessThanOrEqual(3);
  });

  test("signals() honours a limit", () => {
    const d = new AbuseDetector({ maxRequestsPerWindow: 0, windowMs: 1 });
    for (let i = 0; i < 5; i++) d.recordRequest(`k${i}`, 1, i * 1000);
    expect(d.signals(2).length).toBe(2);
  });
});
