// progression.ts — server-side XP / level rules.
//
// This mirrors game-shell/src/progression.cpp exactly: the curve is
// xpForLevel(l) = l*l*100, the cap is level 50, and applying XP in any number of
// increments equals folding the total once (thresholds are monotonic and the
// fold is greedy). tests/progression.test.ts pins that equivalence, so the two
// implementations cannot silently drift.

export const MAX_LEVEL = 50;

/** XP required to advance from `level` to `level + 1` (0 at the cap). */
export function xpForLevel(level: number): number {
  if (level < 1) level = 1;
  if (level >= MAX_LEVEL) return 0;
  return level * level * 100;
}

export interface LevelState {
  level: number;
  xp: number;
}

export interface XpResult extends LevelState {
  levelsGained: number;
}

/** Apply an XP amount to a level state, levelling up (bounded by MAX_LEVEL). */
export function applyXp(state: LevelState, amount: number): XpResult {
  let { level, xp } = state;
  xp += amount;
  if (xp < 0) xp = 0; // XP never goes negative; no de-levelling
  let levelsGained = 0;
  while (level < MAX_LEVEL) {
    const need = xpForLevel(level);
    if (need <= 0 || xp < need) break;
    xp -= need;
    level++;
    levelsGained++;
  }
  return { level, xp, levelsGained };
}

/**
 * Derive the level state from a lifetime XP total. Equivalent to folding every
 * individual grant through applyXp, which is what makes append-only XP storage
 * (ClickHouse) and a live level state agree.
 */
export function levelFromTotal(totalXp: number): XpResult {
  return applyXp({ level: 1, xp: 0 }, totalXp);
}

/** Tier title from the 50-level spec (mirrors progression.cpp). */
export function titleForLevel(level: number): string {
  if (level < 1) level = 1;
  if (level <= 5) return "Beginner";
  if (level <= 10) return "Builder";
  if (level <= 15) return "Engineer";
  if (level <= 20) return "Specialist";
  if (level <= 25) return "Systems Developer";
  if (level <= 30) return "Inventor";
  if (level <= 35) return "Architect";
  if (level <= 40) return "Innovator";
  if (level <= 45) return "Master Engineer";
  return "Creator";
}

/** The pure state behind a profile response. */
export function progressionView(totalXp: number) {
  const { level, xp } = levelFromTotal(totalXp);
  return {
    totalXp,
    level,
    xpIntoLevel: xp,
    xpForNextLevel: xpForLevel(level),
    title: titleForLevel(level),
  };
}
