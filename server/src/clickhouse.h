#pragma once
// clickhouse.h — ClickHouse over its HTTP interface (no driver dependency)
// plus the append-only history seam used by the API.
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "json.h"

namespace dw {

struct ClickHouseConfig {
  std::string url = "http://127.0.0.1:8123";
  std::string user = "devworld";
  std::string password = "devworld_dev_pw";
  std::string database = "devworld";
};

class ClickHouse {
 public:
  explicit ClickHouse(ClickHouseConfig cfg);
  const ClickHouseConfig& config() const { return cfg_; }

  bool command(const std::string& sql,
               const std::map<std::string, std::string>& params,
               std::string* out, std::string* err);
  bool query(const std::string& sql,
             const std::map<std::string, std::string>& params,
             std::vector<JVal>* rows, std::string* err);
  bool insert(const std::string& table, const std::vector<JVal>& rows,
              std::string* err);
  bool ping();

 private:
  std::string endpoint(const std::map<std::string, std::string>& params) const;
  ClickHouseConfig cfg_;
};

// ---- history --------------------------------------------------------------
struct GameEvent {
  std::string playerId;
  std::string event;
  std::string sessionId;
  std::string service;
  std::string props = "{}";  // JSON object text
};

struct XpEventRow {
  std::string playerId;
  std::string source;
  std::string ref;
  int64_t amount = 0;
  int levelAfter = 0;
};

struct LedgerRow {
  std::string ref;
  std::string playerId;
  std::string account;
  int64_t amount = 0;
  std::string currency = "credits";
  std::string memo;
};

struct MissionAttemptRow {
  std::string playerId;
  std::string missionId;
  std::string runtime;
  std::string codeHash;
  bool passed = false;
  int publicPassed = 0;
  int hiddenPassed = 0;
  int total = 0;
  int durationMs = 0;
};

struct AssessmentRow {
  std::string requestId;
  std::string runtime;
  int passed = 0;
  int total = 0;
  int durationMs = 0;
  bool timedOut = false;
  std::string errorKind;
};

struct MissionStat {
  std::string missionId;
  int64_t attempts = 0;
  int64_t passes = 0;
};

class History {
 public:
  virtual ~History() = default;
  virtual bool insertEvents(const std::vector<GameEvent>& rows, std::string* err) = 0;
  virtual bool insertXp(const std::vector<XpEventRow>& rows, std::string* err) = 0;
  virtual bool insertLedger(const std::vector<LedgerRow>& rows, std::string* err) = 0;
  virtual bool insertMissionAttempt(const MissionAttemptRow& row, std::string* err) = 0;
  virtual bool insertAssessment(const AssessmentRow& row, std::string* err) = 0;
  virtual bool playerTotalXp(const std::string& playerId, int64_t* out,
                             std::string* err) = 0;
  virtual bool playerBalance(const std::string& playerId,
                             const std::string& currency, int64_t* out,
                             std::string* err) = 0;
  virtual bool missionStats(std::vector<MissionStat>* out, std::string* err) = 0;
  virtual bool ping() = 0;
};

class ClickHouseHistory : public History {
 public:
  explicit ClickHouseHistory(ClickHouse ch) : ch_(std::move(ch)) {}
  bool insertEvents(const std::vector<GameEvent>& rows, std::string* err) override;
  bool insertXp(const std::vector<XpEventRow>& rows, std::string* err) override;
  bool insertLedger(const std::vector<LedgerRow>& rows, std::string* err) override;
  bool insertMissionAttempt(const MissionAttemptRow& row, std::string* err) override;
  bool insertAssessment(const AssessmentRow& row, std::string* err) override;
  bool playerTotalXp(const std::string& playerId, int64_t* out,
                     std::string* err) override;
  bool playerBalance(const std::string& playerId, const std::string& currency,
                     int64_t* out, std::string* err) override;
  bool missionStats(std::vector<MissionStat>* out, std::string* err) override;
  bool ping() override { return ch_.ping(); }
  ClickHouse& client() { return ch_; }

 private:
  ClickHouse ch_;
};

class MemoryHistory : public History {
 public:
  bool insertEvents(const std::vector<GameEvent>& rows, std::string*) override;
  bool insertXp(const std::vector<XpEventRow>& rows, std::string*) override;
  bool insertLedger(const std::vector<LedgerRow>& rows, std::string*) override;
  bool insertMissionAttempt(const MissionAttemptRow& row, std::string*) override;
  bool insertAssessment(const AssessmentRow& row, std::string*) override;
  bool playerTotalXp(const std::string& playerId, int64_t* out,
                     std::string*) override;
  bool playerBalance(const std::string& playerId, const std::string& currency,
                     int64_t* out, std::string*) override;
  bool missionStats(std::vector<MissionStat>* out, std::string*) override;
  bool ping() override { return healthy; }

  bool healthy = true;
  std::vector<GameEvent> events;
  std::vector<XpEventRow> xp;
  std::vector<LedgerRow> ledger;
  std::vector<MissionAttemptRow> attempts;
  std::vector<AssessmentRow> assessments;

 private:
  std::mutex mu_;
};

}  // namespace dw
