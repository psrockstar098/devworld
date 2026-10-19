import { describe, expect, test } from "bun:test";
import {
  balancesFrom,
  LedgerError,
  MAX_ENTRIES_PER_TRANSACTION,
  netByCurrency,
  payout,
  validateTransaction,
  type LedgerEntry,
} from "../src/ledger";

const balanced = (): LedgerEntry[] => [
  { ref: "t1", playerId: "p1", account: "system", amount: -100 },
  { ref: "t1", playerId: "p1", account: "wallet", amount: 100 },
];

describe("validateTransaction", () => {
  test("accepts a balanced payout", () => {
    expect(() => validateTransaction(payout("p1", "ref", 50))).not.toThrow();
  });

  test("rejects an unbalanced transaction", () => {
    const t = balanced();
    t[1].amount = 90;
    expect(() => validateTransaction(t)).toThrow(LedgerError);
  });

  test("rejects a single entry", () => {
    expect(() => validateTransaction([{ ref: "r", playerId: "p", account: "wallet", amount: 0 }])).toThrow();
  });

  test("rejects zero amounts", () => {
    const t: LedgerEntry[] = [
      { ref: "r", playerId: "p", account: "a", amount: 0 },
      { ref: "r", playerId: "p", account: "b", amount: 0 },
    ];
    expect(() => validateTransaction(t)).toThrow(/zero/);
  });

  test("rejects non-integer amounts", () => {
    const t: LedgerEntry[] = [
      { ref: "r", playerId: "p", account: "a", amount: 1.5 },
      { ref: "r", playerId: "p", account: "b", amount: -1.5 },
    ];
    expect(() => validateTransaction(t)).toThrow(/integer/);
  });

  test("requires one shared ref", () => {
    const t = balanced();
    t[1].ref = "other";
    expect(() => validateTransaction(t)).toThrow(/one ref/);
  });

  test("requires playerId and account", () => {
    const a = balanced();
    a[0].playerId = "";
    expect(() => validateTransaction(a)).toThrow(/playerId/);
    const b = balanced();
    b[0].account = "";
    expect(() => validateTransaction(b)).toThrow(/account/);
  });

  test("balances per currency independently", () => {
    const t: LedgerEntry[] = [
      { ref: "r", playerId: "p", account: "a", amount: 100, currency: "credits" },
      { ref: "r", playerId: "p", account: "b", amount: -100, currency: "credits" },
      { ref: "r", playerId: "p", account: "a", amount: 5, currency: "tokens" },
      { ref: "r", playerId: "p", account: "b", amount: -5, currency: "tokens" },
    ];
    expect(() => validateTransaction(t)).not.toThrow();
    const bad = [...t];
    bad[3] = { ...bad[3], amount: -6 };
    expect(() => validateTransaction(bad)).toThrow(/tokens/);
  });

  test("caps the number of entries", () => {
    const many: LedgerEntry[] = Array.from({ length: MAX_ENTRIES_PER_TRANSACTION + 1 }, () => ({
      ref: "r",
      playerId: "p",
      account: "a",
      amount: 1,
    }));
    expect(() => validateTransaction(many)).toThrow(/exceed/);
  });
});

describe("helpers", () => {
  test("payout is balanced and credits the wallet", () => {
    const t = payout("p1", "r", 25, "reward");
    expect(() => validateTransaction(t)).not.toThrow();
    expect(t.find((e) => e.account === "wallet")?.amount).toBe(25);
    expect(t.find((e) => e.account === "system")?.amount).toBe(-25);
  });

  test("netByCurrency sums to zero for a valid transaction", () => {
    for (const total of netByCurrency(balanced()).values()) expect(total).toBe(0);
  });

  test("balancesFrom derives the wallet balance", () => {
    const entries: LedgerEntry[] = [
      ...payout("p1", "r1", 100),
      ...payout("p1", "r2", 25),
      { ref: "r3", playerId: "p1", account: "wallet", amount: -30 },
      { ref: "r3", playerId: "p1", account: "system", amount: 30 },
    ];
    const b = balancesFrom(entries.filter((e) => e.account === "wallet"));
    expect(b.get("credits")).toBe(95);
  });
});
