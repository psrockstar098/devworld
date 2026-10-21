#include "doctest.h"

#include <cstdlib>

#include "config.h"

using namespace dw;

TEST_CASE("admin key has no default: unset ADMIN_KEY disables admin routes") {
  const char* saved = std::getenv("ADMIN_KEY");
  std::string savedCopy = saved ? saved : "";
  bool hadSaved = saved != nullptr;
  unsetenv("ADMIN_KEY");
  Config c = Config::fromEnv();
  CHECK(c.adminKey.empty());
  setenv("ADMIN_KEY", "explicit_key", 1);
  Config c2 = Config::fromEnv();
  CHECK(c2.adminKey == "explicit_key");
  if (hadSaved) {
    setenv("ADMIN_KEY", savedCopy.c_str(), 1);
  } else {
    unsetenv("ADMIN_KEY");
  }
}
