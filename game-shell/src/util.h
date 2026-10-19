#pragma once
// Small utilities: JSON escaping, string helpers.
#include <string>

namespace dw {

// Escape a string for embedding in JSON (quotes, backslash, control chars).
inline std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  o += '"';
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else o += c;
    }
  }
  o += '"';
  return o;
}

}  // namespace dw
