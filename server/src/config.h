#pragma once
// config.h — environment configuration. Every value has a local-development
// default so the server runs against infra/ with no setup.
#include <string>

#include "clickhouse.h"

namespace dw {

struct Config {
  std::string host = "0.0.0.0";
  int port = 8091;

  ClickHouseConfig clickhouse;
  std::string kvUrl = "redis://:devworld_dev_pw@127.0.0.1:6666";
  std::string redisUrl = "redis://:devworld_dev_pw@127.0.0.1:6379";

  std::string workerUrl;
  std::string workerApiKey;
  std::string adminKey;

  std::string contentDir = "../game-shell/content";
  int sessionTtlSec = 604800;
  int balanceCacheTtlSec = 15;

  static Config fromEnv();
};

}  // namespace dw
