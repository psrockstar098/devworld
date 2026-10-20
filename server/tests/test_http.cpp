#include "doctest.h"

#include <sys/socket.h>
#include <unistd.h>

#include "clickhouse.h"
#include "http.h"
#include "ledger.h"
#include "util.h"

using namespace dw;

TEST_CASE("http url parsing") {
  ParsedUrl u;
  std::string err;
  REQUIRE(parseHttpUrl("http://127.0.0.1:8123/base?a=1", &u, &err));
  CHECK(u.host == "127.0.0.1");
  CHECK(u.port == 8123);
  CHECK(u.path == "/base");
  CHECK(u.query == "a=1");

  REQUIRE(parseHttpUrl("http://example.com", &u, &err));
  CHECK(u.port == 80);
  CHECK(u.path == "/");

  CHECK_FALSE(parseHttpUrl("https://example.com", &u, &err));
}

TEST_CASE("http request parsing handles query, headers and body") {
  int sv[2];
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  Socket sock(sv[0]);
  std::string raw =
      "POST /missions/m01/submit?x=1&y=a%20b HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Authorization: Bearer abc\r\n"
      "Content-Length: 11\r\n"
      "\r\n"
      "hello world";
  REQUIRE(::write(sv[1], raw.data(), raw.size()) == (ssize_t)raw.size());

  std::string buffer, err;
  HttpRequest req;
  REQUIRE(readHttpRequest(sock, &buffer, &req, &err));
  CHECK(req.method == "POST");
  CHECK(req.path == "/missions/m01/submit");
  CHECK(req.param("x") == "1");
  CHECK(req.param("y") == "a b");
  CHECK(req.header("authorization") == "Bearer abc");
  CHECK(req.body == "hello world");
  ::close(sv[1]);
}

TEST_CASE("http request parsing tolerates keep-alive leftovers") {
  int sv[2];
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  Socket sock(sv[0]);
  std::string raw =
      "GET /health HTTP/1.1\r\nHost: x\r\n\r\n"
      "GET /me HTTP/1.1\r\nHost: x\r\n\r\n";
  REQUIRE(::write(sv[1], raw.data(), raw.size()) == (ssize_t)raw.size());
  std::string buffer, err;
  HttpRequest a, b;
  REQUIRE(readHttpRequest(sock, &buffer, &a, &err));
  REQUIRE(readHttpRequest(sock, &buffer, &b, &err));
  CHECK(a.path == "/health");
  CHECK(b.path == "/me");
  ::close(sv[1]);
}

TEST_CASE("response serialisation sets length and connection") {
  HttpResponse r = HttpResponse::json("{\"ok\":true}", 200);
  std::string out = serializeHttpResponse(r, true);
  CHECK(out.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(out.find("content-type: application/json\r\n") != std::string::npos);
  CHECK(out.find("Content-Length: 11\r\n") != std::string::npos);
  CHECK(out.find("Connection: keep-alive\r\n") != std::string::npos);
  CHECK(out.substr(out.size() - 11) == "{\"ok\":true}");
}

TEST_CASE("clickhouse round-trips against the real store when available") {
  ClickHouseConfig cfg;
  ClickHouse ch(cfg);
  if (!ch.ping()) {
    MESSAGE("clickhouse unavailable, skipping");
    return;
  }
  ClickHouseHistory history(ch);
  std::string err;
  std::string pid = "test_" + randomHex(8);

  REQUIRE(history.insertEvents({{pid, "test.event", "", "server-test", "{\"a\":1}"}}, &err));
  REQUIRE(history.insertXp({{pid, "test", "r1", 250, 1}}, &err));
  auto entries = payout(pid, "ref-" + pid, 42, "test");
  std::vector<LedgerRow> rows;
  for (const auto& e : entries)
    rows.push_back({e.ref, e.playerId, e.account, e.amount, e.currency, e.memo});
  REQUIRE(history.insertLedger(rows, &err));

  int64_t xp = -1;
  REQUIRE(history.playerTotalXp(pid, &xp, &err));
  CHECK(xp == 250);
  int64_t bal = -1;
  REQUIRE(history.playerBalance(pid, "credits", &bal, &err));
  CHECK(bal == 42);

  REQUIRE(history.insertMissionAttempt(
      {pid, "m01", "python", "abc", true, 1, 1, 2, 5}, &err));
  std::vector<MissionStat> stats;
  REQUIRE(history.missionStats(&stats, &err));
  bool saw = false;
  for (const auto& s : stats)
    if (s.missionId == "m01") saw = true;
  CHECK(saw);
}
