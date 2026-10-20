#include "ledger.h"

#include <map>

namespace dw {

bool validateTransaction(const std::vector<LedgerEntry>& entries,
                         std::string* err) {
  auto fail = [&](const std::string& m) {
    if (err) *err = m;
    return false;
  };
  if (entries.size() < 2) return fail("a transaction needs at least two entries");
  if (entries.size() > kMaxEntriesPerTransaction)
    return fail("a transaction may not exceed " +
                std::to_string(kMaxEntriesPerTransaction) + " entries");
  const std::string& ref = entries[0].ref;
  if (ref.empty() || ref.size() > 128)
    return fail("transaction ref must be a 1-128 character string");
  std::map<std::string, int64_t> totals;
  for (const auto& e : entries) {
    if (e.ref != ref) return fail("all entries in a transaction share one ref");
    if (e.playerId.empty()) return fail("every entry needs a playerId");
    if (e.account.empty()) return fail("every entry needs an account");
    if (e.amount == 0) return fail("zero-amount entries are not allowed");
    totals[e.currency.empty() ? "credits" : e.currency] += e.amount;
  }
  for (const auto& kv : totals) {
    if (kv.second != 0)
      return fail("transaction " + ref + " is unbalanced in " + kv.first + ": " +
                  std::to_string(kv.second));
  }
  return true;
}

std::vector<LedgerEntry> payout(const std::string& playerId,
                                const std::string& ref, int64_t amount,
                                const std::string& memo) {
  return {
      LedgerEntry{ref, playerId, "system", -amount, "credits", memo},
      LedgerEntry{ref, playerId, "wallet", amount, "credits", memo},
  };
}

}  // namespace dw
