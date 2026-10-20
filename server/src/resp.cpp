#include "resp.h"

#include <cstdlib>

namespace dw {

std::string RespReply::asString() const {
  switch (type) {
    case STATUS:
    case BULK: return str;
    case INTEGER: return std::to_string(integer);
    case NIL: return "";
    default: return "";
  }
}

int64_t RespReply::asInt() const {
  if (type == INTEGER) return integer;
  if (type == BULK || type == STATUS) {
    int64_t v = 0;
    if (!str.empty() && (str[0] == '-' || (str[0] >= '0' && str[0] <= '9'))) {
      char* end = nullptr;
      long long n = std::strtoll(str.c_str(), &end, 10);
      if (end && *end == '\0') v = (int64_t)n;
    }
    return v;
  }
  return 0;
}

RespClient::~RespClient() { close(); }

void RespClient::close() {
  sock_.close();
  buf_.clear();
  pos_ = 0;
}

bool RespClient::connect(const std::string& host, int port,
                         const std::string& password, int timeoutMs,
                         std::string* err) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    close();
    sock_ = Socket::connectTcp(host, port, timeoutMs, err);
    if (!sock_.valid()) return false;
    sock_.setTimeouts(timeoutMs, timeoutMs);
  }
  if (!password.empty()) {
    RespReply r;
    if (!command({"AUTH", password}, &r, err)) return false;
    if (!r.ok()) {
      if (err) *err = "AUTH failed: " + r.str;
      close();
      return false;
    }
  }
  return true;
}

bool RespClient::ensure(size_t n, std::string* err) {
  while (buf_.size() - pos_ < n) {
    char tmp[8192];
    ssize_t got = sock_.readSome(tmp, sizeof tmp);
    if (got <= 0) {
      if (err) *err = got == 0 ? "connection closed" : "read error";
      return false;
    }
    buf_.append(tmp, (size_t)got);
  }
  return true;
}

bool RespClient::readLine(std::string* line, std::string* err) {
  for (;;) {
    size_t end = buf_.find("\r\n", pos_);
    if (end != std::string::npos) {
      *line = buf_.substr(pos_, end - pos_);
      pos_ = end + 2;
      return true;
    }
    if (!ensure(buf_.size() - pos_ + 1, err)) return false;
    // Compact occasionally so a long-lived connection does not grow forever.
    if (pos_ > 1 << 20) {
      buf_.erase(0, pos_);
      pos_ = 0;
    }
  }
}

bool RespClient::readExactN(size_t n, std::string* out, std::string* err) {
  if (!ensure(n + 2, err)) return false;
  *out = buf_.substr(pos_, n);
  pos_ += n + 2;  // consume trailing CRLF
  return true;
}

bool RespClient::readReply(RespReply* out, std::string* err) {
  if (!ensure(1, err)) return false;
  char c = buf_[pos_++];
  std::string line;
  switch (c) {
    case '+':
      if (!readLine(&line, err)) return false;
      out->type = RespReply::STATUS;
      out->str = line;
      return true;
    case '-':
      if (!readLine(&line, err)) return false;
      out->type = RespReply::ERROR;
      out->str = line;
      return true;
    case ':':
      if (!readLine(&line, err)) return false;
      out->type = RespReply::INTEGER;
      out->integer = std::strtoll(line.c_str(), nullptr, 10);
      return true;
    case '$': {
      if (!readLine(&line, err)) return false;
      long long n = std::strtoll(line.c_str(), nullptr, 10);
      if (n < 0) {
        out->type = RespReply::NIL;
        return true;
      }
      std::string val;
      if (!readExactN((size_t)n, &val, err)) return false;
      out->type = RespReply::BULK;
      out->str = std::move(val);
      return true;
    }
    case '*': {
      if (!readLine(&line, err)) return false;
      long long n = std::strtoll(line.c_str(), nullptr, 10);
      if (n < 0) {
        out->type = RespReply::NIL;
        return true;
      }
      out->type = RespReply::ARRAY;
      out->arr.clear();
      out->arr.reserve((size_t)n);
      for (long long i = 0; i < n; ++i) {
        RespReply item;
        if (!readReply(&item, err)) return false;
        out->arr.push_back(std::move(item));
      }
      return true;
    }
    default:
      if (err) *err = std::string("bad RESP byte: ") + c;
      return false;
  }
}

bool RespClient::command(const std::vector<std::string>& args, RespReply* out,
                         std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!sock_.valid()) {
    if (err) *err = "not connected";
    return false;
  }
  std::string wire;
  wire.reserve(64);
  wire += '*';
  wire += std::to_string(args.size());
  wire += "\r\n";
  for (const auto& a : args) {
    wire += '$';
    wire += std::to_string(a.size());
    wire += "\r\n";
    wire += a;
    wire += "\r\n";
  }
  if (!sock_.writeAll(wire.data(), wire.size())) {
    if (err) *err = "write error";
    return false;
  }
  RespReply reply;
  if (!readReply(&reply, err)) {
    close();
    return false;
  }
  if (out) *out = std::move(reply);
  return true;
}

bool RespClient::ping(std::string* err) {
  RespReply r;
  if (!command({"PING"}, &r, err)) return false;
  return r.ok() && r.asString() == "PONG";
}

}  // namespace dw
