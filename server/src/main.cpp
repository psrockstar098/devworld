// main.cpp — DEVWORLD C++ platform server entrypoint.
//
// Wires the real stores (ClickHouse history, Kvrocks durable KV, Redis cache),
// loads the mission catalogue, and serves the HTTP + WebSocket API.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include "api.h"
#include "clickhouse.h"
#include "config.h"
#include "http.h"
#include "kv.h"
#include "missions.h"
#include "players.h"
#include "sessions.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }
}  // namespace

int main() {
  using namespace dw;
  Config cfg = Config::fromEnv();
  std::string err;

  RespKv kv;
  if (!kv.connectUrl(cfg.kvUrl, 3000, &err)) {
    std::fprintf(stderr, "kvrocks: %s\n", err.c_str());
    return 1;
  }
  RespKv cache;
  if (!cache.connectUrl(cfg.redisUrl, 3000, &err)) {
    std::fprintf(stderr, "redis: %s\n", err.c_str());
    return 1;
  }

  ClickHouse clickhouse(cfg.clickhouse);
  ClickHouseHistory history(clickhouse);
  PlayerStore players(&kv);
  SessionStore sessions(&cache, cfg.sessionTtlSec);

  MissionCatalog missions;
  std::string missionsPath = cfg.contentDir + "/missions.json";
  if (!missions.loadFromFile(missionsPath, &err)) {
    std::fprintf(stderr, "content: %s\n", err.c_str());
    return 1;
  }

  AssessClient assess(cfg.workerUrl, cfg.workerApiKey);
  ApiConfig apiCfg;
  apiCfg.adminKey = cfg.adminKey;
  apiCfg.sessionTtlSec = cfg.sessionTtlSec;
  apiCfg.balanceCacheTtlSec = cfg.balanceCacheTtlSec;

  Api api(&history, &kv, &cache, &players, &sessions, &missions,
          assess.configured() ? &assess : nullptr, apiCfg);

  HttpServer server(
      cfg.host, cfg.port,
      [&api](const HttpRequest& req) { return api.handle(req); },
      [&api](Socket& sock, const HttpRequest& req) { api.handleUpgrade(sock, req); });

  if (!server.start(&err)) {
    std::fprintf(stderr, "listen: %s\n", err.c_str());
    return 1;
  }

  std::printf(
      "devworld-game-server (c++) listening on http://%s:%d\n"
      "  clickhouse  %s (db %s)\n"
      "  kvrocks     %s\n"
      "  redis       %s\n"
      "  worker      %s\n"
      "  missions    %zu (%s)\n",
      cfg.host.c_str(), server.port(), cfg.clickhouse.url.c_str(),
      cfg.clickhouse.database.c_str(),
      cfg.kvUrl.substr(0, cfg.kvUrl.find('@') == std::string::npos
                              ? cfg.kvUrl.size()
                              : cfg.kvUrl.find('@'))
          .c_str(),
      cfg.redisUrl.substr(0, cfg.redisUrl.find('@') == std::string::npos
                                  ? cfg.redisUrl.size()
                                  : cfg.redisUrl.find('@'))
          .c_str(),
      assess.configured() ? cfg.workerUrl.c_str()
                          : "(not configured - grading disabled)",
      missions.size(), missionsPath.c_str());
  std::fflush(stdout);

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::printf("shutting down...\n");
  std::fflush(stdout);
  server.stop();
  kv.close();
  cache.close();
  return 0;
}
