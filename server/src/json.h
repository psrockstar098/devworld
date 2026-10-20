#pragma once
// json.h — the server reuses the game's minimal JSON parser (single source of
// truth) and adds the small amount of construction/serialisation the HTTP API
// needs. Everything here is header-only and dependency-free.
#include "../../game-shell/src/json.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace dw {

// ---- construction helpers -------------------------------------------------
inline JVal jnull() { return JVal{}; }

inline JVal jbool(bool b) {
  JVal v;
  v.type = JVal::BOOL;
  v.b = b;
  return v;
}

inline JVal jnum(double n) {
  JVal v;
  v.type = JVal::NUM;
  v.num = n;
  return v;
}

inline JVal jstr(std::string s) {
  JVal v;
  v.type = JVal::STR;
  v.str = std::move(s);
  return v;
}

inline JVal jarr() {
  JVal v;
  v.type = JVal::ARR;
  return v;
}

inline JVal jobj() {
  JVal v;
  v.type = JVal::OBJ;
  return v;
}

// ---- serialisation --------------------------------------------------------
inline std::string jsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          o += buf;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

inline std::string jsonNumber(double n) {
  if (!std::isfinite(n)) return "0";
  if (n == static_cast<double>(static_cast<long long>(n)) &&
      std::fabs(n) < 9.0e15) {
    return std::to_string(static_cast<long long>(n));
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", n);
  return buf;
}

inline void dumpJsonTo(const JVal& v, std::string& o) {
  switch (v.type) {
    case JVal::NUL: o += "null"; break;
    case JVal::BOOL: o += v.b ? "true" : "false"; break;
    case JVal::NUM: o += jsonNumber(v.num); break;
    case JVal::STR: o += '"'; o += jsonEscape(v.str); o += '"'; break;
    case JVal::ARR: {
      o += '[';
      for (size_t i = 0; i < v.arr.size(); ++i) {
        if (i) o += ',';
        dumpJsonTo(v.arr[i], o);
      }
      o += ']';
      break;
    }
    case JVal::OBJ: {
      o += '{';
      bool first = true;
      for (const auto& kv : v.obj) {
        if (!first) o += ',';
        first = false;
        o += '"';
        o += jsonEscape(kv.first);
        o += "\":";
        dumpJsonTo(kv.second, o);
      }
      o += '}';
      break;
    }
  }
}

inline std::string dumpJson(const JVal& v) {
  std::string o;
  dumpJsonTo(v, o);
  return o;
}

// Convenience: set an object member; no-op for non-objects.
inline void jset(JVal& obj, const std::string& key, JVal value) {
  if (obj.type != JVal::OBJ) obj = jobj();
  obj.obj[key] = std::move(value);
}

}  // namespace dw
