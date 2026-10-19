// missions.ts — the mission catalogue (content) and the grading summary.
//
// Content is loaded from game-shell/content/missions.json so the server and the
// game shell agree on one source of truth. Grading itself happens in the sandbox
// worker; this module turns its verdict into the rewards and the client-safe
// response shape (public detail, hidden pass/fail only).

import type { AssessResult, AssessTest, AssessTestResult } from "./assess";

export interface MissionTest {
  name: string;
  stdin: string;
  expected: string;
  hidden: boolean;
}

export interface MissionDef {
  id: string;
  title: string;
  language: string;
  level: number;
  requires: string[];
  xpReward: number;
  creditReward: number;
  gate: boolean;
  tests: MissionTest[];
}

export class MissionContentError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "MissionContentError";
  }
}

/** Parse and validate missions.json into a map keyed by id. */
export function loadMissionCatalog(json: unknown): Map<string, MissionDef> {
  const list = Array.isArray(json)
    ? json
    : ((json as { missions?: unknown })?.missions ?? null);
  if (!Array.isArray(list)) throw new MissionContentError("missions.json must be an array or {missions:[]}");
  const out = new Map<string, MissionDef>();
  for (const raw of list as Record<string, unknown>[]) {
    const id = String(raw.id ?? "");
    if (!/^[a-z0-9_]+$/.test(id)) throw new MissionContentError(`bad mission id: ${id}`);
    if (out.has(id)) throw new MissionContentError(`duplicate mission id: ${id}`);
    const tests = Array.isArray(raw.tests) ? (raw.tests as Record<string, unknown>[]) : null;
    if (!tests || tests.length === 0) throw new MissionContentError(`mission ${id} has no tests`);
    out.set(id, {
      id,
      title: String(raw.title ?? id),
      language: String(raw.language ?? "python"),
      level: Number(raw.level ?? 1),
      requires: Array.isArray(raw.requires) ? (raw.requires as string[]) : [],
      xpReward: Number(raw.xpReward ?? 0),
      creditReward: Number(raw.creditReward ?? 0),
      gate: !!raw.gate,
      tests: tests.map((t) => ({
        name: String(t.name ?? ""),
        stdin: String(t.stdin ?? ""),
        expected: String(t.expected ?? ""),
        hidden: !!t.hidden,
      })),
    });
  }
  return out;
}

/** The tests to hand the sandbox worker (all of them: public and hidden). */
export function assessTestsFor(mission: MissionDef): AssessTest[] {
  return mission.tests.map((t) => ({
    name: t.name,
    stdin: t.stdin,
    expectedStdout: t.expected,
    hidden: t.hidden,
  }));
}

export interface MissionGrading {
  passed: boolean;
  total: number;
  publicPassed: number;
  hiddenPassed: number;
  publicTests: number;
  hiddenTests: number;
  /** Rewards are granted only on a full pass. */
  rewards: { xp: number; credits: number } | null;
  /** Client-safe results: hidden tests never carry actual output. */
  results: { name: string; passed: boolean; hidden: boolean; actual?: string; errorKind?: string }[];
}

const byName = (results: AssessTestResult[]): Map<string, AssessTestResult> =>
  new Map(results.map((r) => [r.name, r]));

/** Combine the mission definition and the worker verdict into the response. */
export function summarize(mission: MissionDef, result: AssessResult): MissionGrading {
  const found = byName(result.results);
  let publicPassed = 0;
  let hiddenPassed = 0;
  let publicTests = 0;
  let hiddenTests = 0;
  const results = mission.tests.map((t) => {
    const r = found.get(t.name);
    const passed = !!r?.passed;
    if (t.hidden) {
      hiddenTests++;
      if (passed) hiddenPassed++;
    } else {
      publicTests++;
      if (passed) publicPassed++;
    }
    return {
      name: t.name,
      passed,
      hidden: t.hidden,
      actual: t.hidden ? undefined : r?.actual,
      errorKind: r?.errorKind,
    };
  });
  const passed = publicPassed === publicTests && hiddenPassed === hiddenTests;
  return {
    passed,
    total: mission.tests.length,
    publicPassed,
    hiddenPassed,
    publicTests,
    hiddenTests,
    rewards: passed ? { xp: mission.xpReward, credits: mission.creditReward } : null,
    results,
  };
}
