#include "util.h"

#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>

namespace dw {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) ++a;
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

std::string toLower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string o;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) o += sep;
    o += parts[i];
  }
  return o;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string urlDecode(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int a = hexVal(s[i + 1]), b = hexVal(s[i + 2]);
      if (a >= 0 && b >= 0) {
        o += (char)((a << 4) | b);
        i += 2;
        continue;
      }
    }
    o += s[i] == '+' ? ' ' : s[i];
  }
  return o;
}

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

bool parseInt(const std::string& s, int* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (!end || *end != '\0') return false;
  *out = (int)v;
  return true;
}

bool parseInt64(const std::string& s, int64_t* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  long long v = std::strtoll(s.c_str(), &end, 10);
  if (!end || *end != '\0') return false;
  *out = (int64_t)v;
  return true;
}

static const char kStdAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char kUrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string b64(const unsigned char* data, size_t len, const char* alphabet,
                       bool pad) {
  std::string o;
  o.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    unsigned v = (unsigned)data[i] << 16 | (unsigned)data[i + 1] << 8 | data[i + 2];
    o += alphabet[(v >> 18) & 63];
    o += alphabet[(v >> 12) & 63];
    o += alphabet[(v >> 6) & 63];
    o += alphabet[v & 63];
  }
  if (i < len) {
    unsigned v = (unsigned)data[i] << 16;
    bool two = i + 1 < len;
    if (two) v |= (unsigned)data[i + 1] << 8;
    o += alphabet[(v >> 18) & 63];
    o += alphabet[(v >> 12) & 63];
    if (two) o += alphabet[(v >> 6) & 63];
    else if (pad) o += '=';
    if (pad) o += '=';
  }
  return o;
}

std::string base64Encode(const unsigned char* data, size_t len) {
  return b64(data, len, kStdAlphabet, true);
}
std::string base64Encode(const std::string& data) {
  return base64Encode((const unsigned char*)data.data(), data.size());
}
std::string base64Encode(const std::vector<unsigned char>& data) {
  return base64Encode(data.data(), data.size());
}
std::string base64UrlEncode(const unsigned char* data, size_t len) {
  return b64(data, len, kUrlAlphabet, false);
}
std::string base64UrlEncode(const std::string& data) {
  return base64UrlEncode((const unsigned char*)data.data(), data.size());
}

bool base64Decode(const std::string& in, std::string* out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::string o;
  int buf = 0, bits = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = val(c);
    if (v < 0) return false;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      o += (char)((buf >> bits) & 0xFF);
    }
  }
  *out = std::move(o);
  return true;
}

bool randomBytes(void* buf, size_t len) {
  unsigned char* p = (unsigned char*)buf;
  size_t got = 0;
  while (got < len) {
    ssize_t n = ::getrandom(p + got, len - got, 0);
    if (n > 0) {
      got += (size_t)n;
      continue;
    }
    // Fallback for kernels without getrandom(2).
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    while (got < len) {
      ssize_t r = ::read(fd, p + got, len - got);
      if (r <= 0) break;
      got += (size_t)r;
    }
    ::close(fd);
  }
  return got == len;
}

std::string randomHex(size_t bytes) {
  std::string raw(bytes, '\0');
  if (!randomBytes(&raw[0], bytes)) return std::string();
  static const char* hex = "0123456789abcdef";
  std::string o;
  o.reserve(bytes * 2);
  for (unsigned char c : raw) {
    o += hex[c >> 4];
    o += hex[c & 15];
  }
  return o;
}

std::string randomBase64Url(size_t bytes) {
  std::string raw(bytes, '\0');
  if (!randomBytes(&raw[0], bytes)) return std::string();
  return base64UrlEncode(raw);
}

int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string isoFromMs(int64_t ms) {
  std::time_t sec = (std::time_t)(ms / 1000);
  int millis = (int)(ms % 1000);
  if (millis < 0) millis += 1000;
  std::tm tm{};
  gmtime_r(&sec, &tm);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, millis);
  return buf;
}

std::string isoNow() { return isoFromMs(nowMs()); }

std::string queryEscape(const std::string& s) { return urlEncode(s); }

}  // namespace dw
