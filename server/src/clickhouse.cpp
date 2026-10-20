#include "clickhouse.h"

#include <cstdlib>
#include <map>
#include <set>

#include "http.h"
#include "util.h"

namespace dw {
namespace {

bool quoteTable(const std::string& table, std::string* out, std::string* err) {
  auto parts = split(table, '.');
  if (parts.empty() || parts.size() > 2) {
    if (err) *err = "unsafe table name: " + table;
    return false;
  }
  std::string q;
  for (size_t i = 0; i < parts.size(); ++i) {
    const std::string& p = parts[i];
    if (p.empty() || !(std::isalpha((unsigned char)p[0]) || p[0] == '_')) {
      if (err) *err = "unsafe table name: " + table;
      return false;
    }
    for (char c : p) {
      if (!(std::isalnum((unsigned char)c) || c == '_')) {
        if (err) *err = "unsafe table name: " + table;
        return false;
      }
    }
    if (i) q += ".";
    q += "`" + p + "`";
  }
  *out = q;
  return true;
}

int64_t numOf(const JVal& v) {
  if (v.type == JVal::NUM) return (int64_t)v.num;
  if (v.type == JVal::STR) {
    char* end = nullptr;
    long long n = std::strtoll(v.str.c_str(), &end, 10);
    if (end && *end == '\0') return (int64_t)n;
    double d = std::strtod(v.str.c_str(), nullptr);
    return (int64_t)d;
  }
  return 0;
}

JVal row(std::initializer_list<std::pair<const char*, JVal>> items) {
  JVal o = jobj();
  for (const auto& kv : items) o.obj[kv.first] = kv.second;
  return o;
}

}  // namespace

ClickHouse::ClickHouse(ClickHouseConfig cfg) : cfg_(std::move(cfg)) {
  if (!cfg_.url.empty() && cfg_.url.back() == '/') cfg_.url.pop_back();
}

std::string ClickHouse::endpoint(
    const std::map<std::string, std::string>& params) const {
  std::string url = cfg_.url + "/?database=" + queryEscape(cfg_.database) +
                    "&wait_end_of_query=1";
  for (const auto& kv : params) {
    url += "&param_" + queryEscape(kv.first) + "=" + queryEscape(kv.second);
  }
  return url;
}

bool ClickHouse::command(const std::string& sql,
                         const std::map<std::string, std::string>& params,
                         std::string* out, std::string* err) {
  std::map<std::string, std::string> headers;
  headers["X-ClickHouse-User"] = cfg_.user;
  headers["X-ClickHouse-Key"] = cfg_.password;
  headers["Content-Type"] = "text/plain; charset=utf-8";
  HttpClientResponse res;
  if (!httpRequest(endpoint(params), "POST", headers, sql, &res, err)) return false;
  if (res.status < 200 || res.status >= 300) {
    if (err) *err = trim(res.body).empty() ? ("ClickHouse HTTP " + std::to_string(res.status))
                                           : trim(res.body);
    return false;
  }
  if (out) *out = res.body;
  return true;
}

bool ClickHouse::query(const std::string& sql,
                       const std::map<std::string, std::string>& params,
                       std::vector<JVal>* rows, std::string* err) {
  std::string q = sql;
  while (!q.empty() && (q.back() == ';' || std::isspace((unsigned char)q.back())))
    q.pop_back();
  std::string text;
  if (!command(q + "\nFORMAT JSONEachRow", params, &text, err)) return false;
  rows->clear();
  for (const auto& line : split(text, '\n')) {
    std::string t = trim(line);
    if (t.empty()) continue;
    try {
      rows->push_back(parseJson(t));
    } catch (const std::exception& e) {
      if (err) *err = std::string("bad JSONEachRow: ") + e.what();
      return false;
    }
  }
  return true;
}

bool ClickHouse::insert(const std::string& table, const std::vector<JVal>& rows,
                        std::string* err) {
  if (rows.empty()) return true;
  std::string q;
  if (!quoteTable(table, &q, err)) return false;
  std::string body = "INSERT INTO " + q + " FORMAT JSONEachRow\n";
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i) body += "\n";
    body += dumpJson(rows[i]);
  }
  std::string out;
  return command(body, {}, &out, err);
}

bool ClickHouse::ping() {
  std::string out;
  std::string err;
  return command("SELECT 1", {}, &out, &err);
}

// ---- ClickHouseHistory ----------------------------------------------------

bool ClickHouseHistory::insertEvents(const std::vector<GameEvent>& rows,
                                     std::string* err) {
  std::vector<JVal> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    out.push_back(row({{"player_id", jstr(r.playerId)},
                       {"session_id", jstr(r.sessionId)},
                       {"event", jstr(r.event)},
                       {"service", jstr(r.service)},
                       {"props", jstr(r.props.empty() ? "{}" : r.props)}}));
  }
  return ch_.insert("events", out, err);
}

bool ClickHouseHistory::insertXp(const std::vector<XpEventRow>& rows,
                                 std::string* err) {
  std::vector<JVal> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    out.push_back(row({{"player_id", jstr(r.playerId)},
                       {"source", jstr(r.source)},
                       {"ref", jstr(r.ref)},
                       {"amount", jnum((double)r.amount)},
                       {"level_after", jnum((double)r.levelAfter)}}));
  }
  return ch_.insert("xp_events", out, err);
}

bool ClickHouseHistory::insertLedger(const std::vector<LedgerRow>& rows,
                                     std::string* err) {
  std::vector<JVal> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    out.push_back(row({{"ref", jstr(r.ref)},
                       {"player_id", jstr(r.playerId)},
                       {"account", jstr(r.account)},
                       {"amount", jnum((double)r.amount)},
                       {"currency", jstr(r.currency.empty() ? "credits" : r.currency)},
                       {"memo", jstr(r.memo)}}));
  }
  return ch_.insert("ledger_entries", out, err);
}

bool ClickHouseHistory::insertMissionAttempt(const MissionAttemptRow& r,
                                             std::string* err) {
  return ch_.insert(
      "mission_attempts",
      {row({{"player_id", jstr(r.playerId)},
            {"mission_id", jstr(r.missionId)},
            {"runtime", jstr(r.runtime)},
            {"code_hash", jstr(r.codeHash)},
            {"passed", jnum(r.passed ? 1 : 0)},
            {"public_passed", jnum(r.publicPassed)},
            {"hidden_passed", jnum(r.hiddenPassed)},
            {"total", jnum(r.total)},
            {"duration_ms", jnum(r.durationMs)}})},
      err);
}

bool ClickHouseHistory::insertAssessment(const AssessmentRow& r,
                                         std::string* err) {
  return ch_.insert(
      "assessment_events",
      {row({{"request_id", jstr(r.requestId)},
            {"runtime", jstr(r.runtime)},
            {"passed", jnum(r.passed)},
            {"total", jnum(r.total)},
            {"duration_ms", jnum(r.durationMs)},
            {"timed_out", jnum(r.timedOut ? 1 : 0)},
            {"error_kind", jstr(r.errorKind)}})},
      err);
}

bool ClickHouseHistory::playerTotalXp(const std::string& playerId, int64_t* out,
                                      std::string* err) {
  std::vector<JVal> rows;
  if (!ch_.query("SELECT sum(xp) AS xp FROM mv_player_xp WHERE player_id = {p:String}",
                 {{"p", playerId}}, &rows, err))
    return false;
  *out = rows.empty() ? 0 : numOf(rows[0].at("xp"));
  return true;
}

bool ClickHouseHistory::playerBalance(const std::string& playerId,
                                      const std::string& currency, int64_t* out,
                                      std::string* err) {
  std::vector<JVal> rows;
  if (!ch_.query(
          "SELECT sum(balance) AS b FROM mv_player_balance WHERE player_id = "
          "{p:String} AND account = 'wallet' AND currency = {c:String}",
          {{"p", playerId}, {"c", currency}}, &rows, err))
    return false;
  *out = rows.empty() ? 0 : numOf(rows[0].at("b"));
  return true;
}

bool ClickHouseHistory::missionStats(std::vector<MissionStat>* out,
                                     std::string* err) {
  std::vector<JVal> rows;
  if (!ch_.query(
          "SELECT mission_id, countMerge(attempts) AS attempts, "
          "sumMerge(passes) AS passes FROM mv_mission_pass GROUP BY mission_id "
          "ORDER BY mission_id",
          {}, &rows, err))
    return false;
  out->clear();
  for (const auto& r : rows) {
    MissionStat s;
    s.missionId = r.s("mission_id");
    s.attempts = numOf(r.at("attempts"));
    s.passes = numOf(r.at("passes"));
    out->push_back(std::move(s));
  }
  return true;
}

// ---- MemoryHistory --------------------------------------------------------

bool MemoryHistory::insertEvents(const std::vector<GameEvent>& rows, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  events.insert(events.end(), rows.begin(), rows.end());
  return true;
}
bool MemoryHistory::insertXp(const std::vector<XpEventRow>& rows, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  xp.insert(xp.end(), rows.begin(), rows.end());
  return true;
}
bool MemoryHistory::insertLedger(const std::vector<LedgerRow>& rows, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  ledger.insert(ledger.end(), rows.begin(), rows.end());
  return true;
}
bool MemoryHistory::insertMissionAttempt(const MissionAttemptRow& r, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  attempts.push_back(r);
  return true;
}
bool MemoryHistory::insertAssessment(const AssessmentRow& r, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  assessments.push_back(r);
  return true;
}
bool MemoryHistory::playerTotalXp(const std::string& playerId, int64_t* out,
                                  std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  int64_t n = 0;
  for (const auto& r : xp)
    if (r.playerId == playerId) n += r.amount;
  *out = n;
  return true;
}
bool MemoryHistory::playerBalance(const std::string& playerId,
                                  const std::string& currency, int64_t* out,
                                  std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  int64_t n = 0;
  for (const auto& r : ledger) {
    if (r.playerId == playerId && r.account == "wallet" &&
        (r.currency.empty() ? "credits" : r.currency) == currency)
      n += r.amount;
  }
  *out = n;
  return true;
}
bool MemoryHistory::missionStats(std::vector<MissionStat>* out, std::string*) {
  std::lock_guard<std::mutex> lock(mu_);
  std::map<std::string, MissionStat> by;
  for (const auto& a : attempts) {
    auto& s = by[a.missionId];
    s.missionId = a.missionId;
    s.attempts++;
    if (a.passed) s.passes++;
  }
  out->clear();
  for (auto& kv : by) out->push_back(kv.second);
  return true;
}

}  // namespace dw
