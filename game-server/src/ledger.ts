// ledger.ts — pure double-entry rules for the money layer.
//
// Money is never stored as a mutable balance. Every movement is a transaction:
// a set of signed rows sharing one `ref`, which must sum to zero per currency.
// Balances are derived (ClickHouse `mv_player_balance` or redisCache). This is
// what makes wallets auditable and makes "credits came from nowhere" impossible.

export interface LedgerEntry {
  ref: string;
  playerId: string;
  /** wallet | escrow | housing | system | … */
  account: string;
  /** Signed integer, in minor units. */
  amount: number;
  currency?: string;
  memo?: string;
}

export const MAX_ENTRIES_PER_TRANSACTION = 100;

export class LedgerError extends Error {
  readonly status = 400;
  constructor(message: string) {
    super(message);
    this.name = "LedgerError";
  }
}

/** Throw if the transaction is not a well-formed, balanced double entry. */
export function validateTransaction(entries: LedgerEntry[]): void {
  if (!Array.isArray(entries) || entries.length < 2) {
    throw new LedgerError("a transaction needs at least two entries");
  }
  if (entries.length > MAX_ENTRIES_PER_TRANSACTION) {
    throw new LedgerError(`a transaction may not exceed ${MAX_ENTRIES_PER_TRANSACTION} entries`);
  }
  const ref = entries[0].ref;
  if (!ref || typeof ref !== "string" || ref.length > 128) {
    throw new LedgerError("transaction ref must be a 1-128 character string");
  }
  const totals = new Map<string, number>();
  for (const e of entries) {
    if (e.ref !== ref) throw new LedgerError("all entries in a transaction share one ref");
    if (!e.playerId) throw new LedgerError("every entry needs a playerId");
    if (!e.account) throw new LedgerError("every entry needs an account");
    if (!Number.isInteger(e.amount)) throw new LedgerError("amounts must be integers");
    if (e.amount === 0) throw new LedgerError("zero-amount entries are not allowed");
    const currency = e.currency ?? "credits";
    totals.set(currency, (totals.get(currency) ?? 0) + e.amount);
  }
  for (const [currency, total] of totals) {
    if (total !== 0) {
      throw new LedgerError(`transaction ${ref} is unbalanced in ${currency}: ${total}`);
    }
  }
}

/** Signed net per currency (all zero for a valid transaction). */
export function netByCurrency(entries: LedgerEntry[]): Map<string, number> {
  const out = new Map<string, number>();
  for (const e of entries) {
    const currency = e.currency ?? "credits";
    out.set(currency, (out.get(currency) ?? 0) + e.amount);
  }
  return out;
}

/** Derive balances from a set of entries, keyed by `currency`. */
export function balancesFrom(entries: LedgerEntry[]): Map<string, number> {
  return netByCurrency(entries);
}

/** Convenience: a balanced system→wallet payout (credits from the void). */
export function payout(playerId: string, ref: string, amount: number, memo = ""): LedgerEntry[] {
  return [
    { ref, playerId, account: "system", amount: -amount, memo },
    { ref, playerId, account: "wallet", amount, memo },
  ];
}
