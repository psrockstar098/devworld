#pragma once
// ledger.h — pure double-entry rules. Money is never a mutable balance: every
// movement is a set of signed rows sharing one `ref` that sums to zero.
#include <cstdint>
#include <string>
#include <vector>

namespace dw {

struct LedgerEntry {
  std::string ref;
  std::string playerId;
  std::string account;  // wallet | escrow | housing | system
  int64_t amount = 0;   // signed, minor units
  std::string currency = "credits";
  std::string memo;
};

constexpr size_t kMaxEntriesPerTransaction = 100;

bool validateTransaction(const std::vector<LedgerEntry>& entries,
                         std::string* err);

// A balanced system → wallet payout.
std::vector<LedgerEntry> payout(const std::string& playerId,
                                const std::string& ref, int64_t amount,
                                const std::string& memo = "");

}  // namespace dw
