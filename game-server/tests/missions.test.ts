import { describe, expect, test } from "bun:test";
import {
  assessTestsFor,
  loadMissionCatalog,
  MissionContentError,
  summarize,
  type MissionDef,
} from "../src/missions";
import type { AssessResult } from "../src/assess";

const catalogJson = {
  missions: [
    {
      id: "m01",
      title: "First Boot",
      language: "python",
      level: 1,
      requires: [],
      xpReward: 150,
      creditReward: 10,
      gate: false,
      tests: [
        { name: "public greeting", stdin: "", expected: "Hello!", hidden: false },
        { name: "hidden edge", stdin: "", expected: "Hello, world!", hidden: true },
      ],
    },
  ],
};

const mission: MissionDef = loadMissionCatalog(catalogJson).get("m01")!;

const verdict = (over: Partial<AssessResult> = {}): AssessResult => ({
  passed: 2,
  total: 2,
  results: [
    { name: "public greeting", passed: true, hidden: false, actual: "Hello!" },
    { name: "hidden edge", passed: true, hidden: true },
  ],
  ...over,
});

describe("loadMissionCatalog", () => {
  test("loads from an array or {missions:[]}", () => {
    expect(loadMissionCatalog(catalogJson.missions as unknown[]).size).toBe(1);
    expect(loadMissionCatalog(catalogJson).get("m01")?.title).toBe("First Boot");
  });

  test("rejects bad input", () => {
    expect(() => loadMissionCatalog({ nope: 1 })).toThrow(MissionContentError);
    expect(() => loadMissionCatalog([{ id: "BAD ID", tests: [{}] }])).toThrow(/bad mission id/);
    expect(() => loadMissionCatalog([{ id: "ok", tests: [] }])).toThrow(/no tests/);
    expect(() =>
      loadMissionCatalog([
        { id: "dup", tests: [{}] },
        { id: "dup", tests: [{}] },
      ]),
    ).toThrow(/duplicate/);
  });

  test("defaults optional fields", () => {
    const m = loadMissionCatalog([{ id: "x", tests: [{ name: "t" }] }]).get("x")!;
    expect(m.language).toBe("python");
    expect(m.level).toBe(1);
    expect(m.requires).toEqual([]);
    expect(m.tests[0]).toEqual({ name: "t", stdin: "", expected: "", hidden: false });
  });
});

describe("assessTestsFor", () => {
  test("maps the content shape to the worker contract", () => {
    const tests = assessTestsFor(mission);
    expect(tests).toEqual([
      { name: "public greeting", stdin: "", expectedStdout: "Hello!", hidden: false },
      { name: "hidden edge", stdin: "", expectedStdout: "Hello, world!", hidden: true },
    ]);
  });
});

describe("summarize", () => {
  test("full pass grants rewards and splits public/hidden counts", () => {
    const g = summarize(mission, verdict());
    expect(g.passed).toBe(true);
    expect(g.publicPassed).toBe(1);
    expect(g.hiddenPassed).toBe(1);
    expect(g.rewards).toEqual({ xp: 150, credits: 10 });
  });

  test("hidden failure blocks rewards", () => {
    const g = summarize(
      mission,
      verdict({
        passed: 1,
        total: 2,
        results: [
          { name: "public greeting", passed: true, hidden: false, actual: "Hello!" },
          { name: "hidden edge", passed: false, hidden: true, errorKind: "wrong-output" },
        ],
      }),
    );
    expect(g.passed).toBe(false);
    expect(g.publicPassed).toBe(1);
    expect(g.hiddenPassed).toBe(0);
    expect(g.rewards).toBeNull();
  });

  test("never exposes hidden actual output", () => {
    const g = summarize(
      mission,
      verdict({
        results: [
          { name: "public greeting", passed: true, hidden: false, actual: "Hello!" },
          // A misbehaving worker trying to leak hidden detail:
          { name: "hidden edge", passed: false, hidden: true, actual: "SECRET", errorKind: "wrong-output" },
        ],
      }),
    );
    const hidden = g.results.find((r) => r.hidden)!;
    expect(hidden.actual).toBeUndefined();
    expect(JSON.stringify(g.results)).not.toContain("SECRET");
  });

  test("a missing result counts as a failure", () => {
    const g = summarize(mission, { passed: 1, total: 2, results: [{ name: "public greeting", passed: true, hidden: false }] });
    expect(g.passed).toBe(false);
    expect(g.hiddenPassed).toBe(0);
  });
});
