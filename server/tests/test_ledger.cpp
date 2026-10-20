#include "doctest.h"

#include "ledger.h"

using namespace dw;

TEST_CASE("a valid payout is balanced") {
  auto entries = payout("p1", "ref-1", 25, "reward");
  std::string err;
  CHECK(validateTransaction(entries, &err));
  CHECK(entries.size() == 2);
  CHECK(entries[0].account == "system");
  CHECK(entries[0].amount == -25);
  CHECK(entries[1].account == "wallet");
  CHECK(entries[1].amount == 25);
}

TEST_CASE("unbalanced or malformed transactions are rejected") {
  std::string err;
  std::vector<LedgerEntry> one = {LedgerEntry{"r", "p", "wallet", 5, "credits", ""}};
  CHECK_FALSE(validateTransaction(one, &err));
  CHECK(err.find("at least two") != std::string::npos);

  std::vector<LedgerEntry> bad = {
      LedgerEntry{"r", "p", "wallet", 5, "credits", ""},
      LedgerEntry{"r", "p", "system", -4, "credits", ""},
  };
  CHECK_FALSE(validateTransaction(bad, &err));
  CHECK(err.find("unbalanced") != std::string::npos);

  std::vector<LedgerEntry> mixed = {
      LedgerEntry{"r", "p", "wallet", 5, "credits", ""},
      LedgerEntry{"r", "p", "system", -5, "gold", ""},
  };
  CHECK_FALSE(validateTransaction(mixed, &err));

  std::vector<LedgerEntry> zero = {
      LedgerEntry{"r", "p", "wallet", 0, "credits", ""},
      LedgerEntry{"r", "p", "system", 0, "credits", ""},
  };
  CHECK_FALSE(validateTransaction(zero, &err));

  std::vector<LedgerEntry> refs = {
      LedgerEntry{"r1", "p", "wallet", 5, "credits", ""},
      LedgerEntry{"r2", "p", "system", -5, "credits", ""},
  };
  CHECK_FALSE(validateTransaction(refs, &err));
}

TEST_CASE("multi-currency balanced transactions pass") {
  std::vector<LedgerEntry> entries = {
      {"r", "p", "wallet", 10, "credits", ""},
      {"r", "p", "system", -10, "credits", ""},
      {"r", "p", "wallet", 3, "gold", ""},
      {"r", "p", "system", -3, "gold", ""},
  };
  std::string err;
  CHECK(validateTransaction(entries, &err));
}
