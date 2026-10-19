import { describe, expect, test } from "bun:test";
import {
  applyXp,
  levelFromTotal,
  MAX_LEVEL,
  progressionView,
  titleForLevel,
  xpForLevel,
} from "../src/progression";

describe("xpForLevel", () => {
  test("is l*l*100 (matches progression.cpp)", () => {
    expect(xpForLevel(1)).toBe(100);
    expect(xpForLevel(2)).toBe(400);
    expect(xpForLevel(10)).toBe(10000);
    expect(xpForLevel(49)).toBe(49 * 49 * 100);
  });

  test("clamps low levels and returns 0 at the cap", () => {
    expect(xpForLevel(0)).toBe(100);
    expect(xpForLevel(-5)).toBe(100);
    expect(xpForLevel(MAX_LEVEL)).toBe(0);
    expect(xpForLevel(60)).toBe(0);
  });
});

describe("applyXp", () => {
  test("accumulates below a threshold", () => {
    const r = applyXp({ level: 1, xp: 0 }, 50);
    expect(r).toEqual({ level: 1, xp: 50, levelsGained: 0 });
  });

  test("levels up and keeps the remainder", () => {
    const r = applyXp({ level: 1, xp: 0 }, 250);
    expect(r.level).toBe(2);
    expect(r.xp).toBe(150);
    expect(r.levelsGained).toBe(1);
  });

  test("can gain several levels at once", () => {
    // 100 (l1) + 400 (l2) + 900 (l3) = 1400 to reach level 4
    const r = applyXp({ level: 1, xp: 0 }, 1500);
    expect(r.level).toBe(4);
    expect(r.xp).toBe(100);
    expect(r.levelsGained).toBe(3);
  });

  test("never levels past the cap", () => {
    const r = applyXp({ level: MAX_LEVEL, xp: 0 }, 1_000_000);
    expect(r.level).toBe(MAX_LEVEL);
    expect(r.levelsGained).toBe(0);
  });

  test("never goes negative", () => {
    const r = applyXp({ level: 3, xp: 10 }, -999);
    expect(r.xp).toBe(0);
    expect(r.level).toBe(3);
  });
});

describe("levelFromTotal equivalence", () => {
  test("folding one total equals applying increments", () => {
    const grants = [150, 400, 900, 1234, 1, 9999, 250];
    let state = { level: 1, xp: 0 };
    let total = 0;
    for (const g of grants) {
      state = applyXp(state, g);
      total += g;
    }
    const folded = levelFromTotal(total);
    expect(folded.level).toBe(state.level);
    expect(folded.xp).toBe(state.xp);
  });

  test("zero total is level 1 with no xp", () => {
    expect(levelFromTotal(0)).toEqual({ level: 1, xp: 0, levelsGained: 0 });
  });
});

describe("titleForLevel", () => {
  test("maps tiers exactly like progression.cpp", () => {
    expect(titleForLevel(1)).toBe("Beginner");
    expect(titleForLevel(5)).toBe("Beginner");
    expect(titleForLevel(6)).toBe("Builder");
    expect(titleForLevel(16)).toBe("Specialist");
    expect(titleForLevel(25)).toBe("Systems Developer");
    expect(titleForLevel(26)).toBe("Inventor");
    expect(titleForLevel(50)).toBe("Creator");
    expect(titleForLevel(0)).toBe("Beginner");
  });
});

describe("progressionView", () => {
  test("reports level, remainder and the next threshold", () => {
    const v = progressionView(150);
    expect(v.totalXp).toBe(150);
    expect(v.level).toBe(2);
    expect(v.xpIntoLevel).toBe(50);
    expect(v.xpForNextLevel).toBe(400);
    expect(v.title).toBe("Beginner");
  });
});
