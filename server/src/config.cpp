#include "config.h"

#include <cstdlib>

#include "util.h"

namespace dw {
namespace {

std::string env(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return v && *v ? std::string(v) : fallback;
}

int envInt(const char* name, int fallback) {
  const char* v = std::getenv(name);
  if (!v || !*v) return fallback;
  int n = 0;
  return parseInt(v, &n) && n > 0 ? n : fallback;
}

}  // namespace

Config Config::fromEnv() {
  Config c;
  c.host = env("HOST", c.host);
  c.port = envInt("PORT", c.port);
  c.clickhouse.url = env("CLICKHOUSE_URL", c.clickhouse.url);
  c.clickhouse.user = env("CLICKHOUSE_USER", c.clickhouse.user);
  c.clickhouse.password = env("CLICKHOUSE_PASSWORD", c.clickhouse.password);
  c.clickhouse.database = env("CLICKHOUSE_DB", c.clickhouse.database);

  std::string redisPassword = env("REDIS_PASSWORD", "devworld_dev_pw");
  std::string kvPassword = env("KV_PASSWORD", "devworld_dev_pw");
  c.redisUrl = env("REDIS_URL", "redis://:" + redisPassword + "@127.0.0.1:" +
                                   env("REDIS_PORT", "6379"));
  c.kvUrl = env("KV_URL", "redis://:" + kvPassword + "@127.0.0.1:" +
                               env("KV_PORT", "6666"));

  c.workerUrl = env("WORKER_URL", "");
  c.workerApiKey = env("WORKER_API_KEY", "");
  c.adminKey = env("ADMIN_KEY", "dev_admin_key");
  c.contentDir = env("CONTENT_DIR", c.contentDir);
  c.sessionTtlSec = envInt("SESSION_TTL_SEC", c.sessionTtlSec);
  c.balanceCacheTtlSec = envInt("BALANCE_CACHE_TTL_SEC", c.balanceCacheTtlSec);
  c.oauth = OAuthConfig::fromEnv();
  return c;
}

}  // namespace dw
