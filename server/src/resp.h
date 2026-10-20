#pragma once
// resp.h — a minimal RESP2 client, enough for Redis and Kvrocks.
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "net.h"

namespace dw {

struct RespReply {
  enum Type { STATUS, ERROR, INTEGER, BULK, ARRAY, NIL } type = NIL;
  std::string str;
  int64_t integer = 0;
  std::vector<RespReply> arr;

  bool ok() const { return type != ERROR; }
  bool isNil() const { return type == NIL; }
  std::string asString() const;
  int64_t asInt() const;
};

class RespClient {
 public:
  RespClient() = default;
  ~RespClient();

  RespClient(const RespClient&) = delete;
  RespClient& operator=(const RespClient&) = delete;

  bool connect(const std::string& host, int port, const std::string& password,
               int timeoutMs, std::string* err);
  void close();
  bool isConnected() const { return sock_.valid(); }

  bool command(const std::vector<std::string>& args, RespReply* out,
               std::string* err);
  bool ping(std::string* err);

 private:
  bool ensure(size_t n, std::string* err);
  bool readLine(std::string* line, std::string* err);
  bool readExactN(size_t n, std::string* out, std::string* err);
  bool readReply(RespReply* out, std::string* err);

  Socket sock_;
  std::string buf_;
  size_t pos_ = 0;
  std::mutex mu_;
};

}  // namespace dw
