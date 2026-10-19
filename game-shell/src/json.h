#pragma once
// Minimal JSON parser for game content files. Handles objects, arrays,
// strings (with escapes), numbers, true/false/null. Not a general parser —
// it assumes well-formed content authored for this game.

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>

namespace dw {

struct JVal {
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JVal> arr;
  std::map<std::string, JVal> obj;

  bool isNull() const { return type == NUL; }
  const JVal& at(const std::string& k) const {
    static JVal nullv;
    auto it = obj.find(k);
    return it == obj.end() ? nullv : it->second;
  }
  const JVal& at(size_t i) const {
    static JVal nullv;
    return i < arr.size() ? arr[i] : nullv;
  }
  std::string s(const std::string& k, const std::string& d = "") const {
    const JVal& v = at(k);
    return v.type == STR ? v.str : d;
  }
  double n(const std::string& k, double d = 0) const {
    const JVal& v = at(k);
    return v.type == NUM ? v.num : d;
  }
  int i(const std::string& k, int d = 0) const { return (int)n(k, d); }
  bool boolean(const std::string& k, bool d = false) const {
    const JVal& v = at(k);
    return v.type == BOOL ? v.b : d;
  }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& text) : s_(text) {}
  JVal parse() {
    skip();
    JVal v = value();
    skip();
    return v;
  }

 private:
  const std::string& s_;
  size_t p_ = 0;
  [[noreturn]] void fail(const std::string& m) { throw std::runtime_error("JSON: " + m); }
  void skip() {
    while (p_ < s_.size() && isspace((unsigned char)s_[p_])) p_++;
  }
  char peek() { return p_ < s_.size() ? s_[p_] : '\0'; }
  char get() {
    if (p_ >= s_.size()) fail("unexpected end");
    return s_[p_++];
  }
  void expect(char c) {
    if (get() != c) fail(std::string("expected '") + c + "'");
  }
  JVal value() {
    skip();
    char c = peek();
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') { JVal v; v.type = JVal::STR; v.str = string(); return v; }
    if (c == 't' || c == 'f') return boolean();
    if (c == 'n') { expect('n'); expect('u'); expect('l'); expect('l'); return JVal(); }
    return number();
  }
  JVal object() {
    JVal v; v.type = JVal::OBJ;
    expect('{'); skip();
    if (peek() == '}') { get(); return v; }
    while (true) {
      skip();
      std::string k = string();
      skip(); expect(':');
      v.obj[k] = value();
      skip();
      char c = get();
      if (c == '}') break;
      if (c != ',') fail("expected ',' or '}'");
    }
    return v;
  }
  JVal array() {
    JVal v; v.type = JVal::ARR;
    expect('['); skip();
    if (peek() == ']') { get(); return v; }
    while (true) {
      v.arr.push_back(value());
      skip();
      char c = get();
      if (c == ']') break;
      if (c != ',') fail("expected ',' or ']'");
    }
    return v;
  }
  std::string string() {
    expect('"');
    std::string o;
    while (true) {
      char c = get();
      if (c == '"') break;
      if (c == '\\') {
        char e = get();
        if (e == 'n') o += '\n';
        else if (e == 't') o += '\t';
        else if (e == 'r') o += '\r';
        else if (e == 'u') {
          // Basic \uXXXX -> UTF-8 (BMP only; enough for our content).
          unsigned cp = 0;
          for (int i = 0; i < 4; i++) {
            char h = get();
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= h - '0';
            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
          }
          if (cp < 0x80) o += (char)cp;
          else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
          else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        }
        else o += e;
      } else o += c;
    }
    return o;
  }
  JVal boolean() {
    JVal v; v.type = JVal::BOOL;
    if (peek() == 't') { expect('t'); expect('r'); expect('u'); expect('e'); v.b = true; }
    else { expect('f'); expect('a'); expect('l'); expect('s'); expect('e'); v.b = false; }
    return v;
  }
  JVal number() {
    size_t st = p_;
    if (peek() == '-') get();
    while (isdigit((unsigned char)peek())) get();
    if (peek() == '.') { get(); while (isdigit((unsigned char)peek())) get(); }
    if (peek() == 'e' || peek() == 'E') {
      get();
      if (peek() == '+' || peek() == '-') get();
      while (isdigit((unsigned char)peek())) get();
    }
    JVal v; v.type = JVal::NUM;
    v.num = std::stod(s_.substr(st, p_ - st));
    return v;
  }
};

inline JVal parseJson(const std::string& text) { return JsonParser(text).parse(); }

}  // namespace dw
