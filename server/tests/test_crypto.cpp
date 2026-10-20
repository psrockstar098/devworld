#include "doctest.h"

#include "crypto.h"
#include "util.h"

using namespace dw;

TEST_CASE("sha256 known vectors") {
  CHECK(sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256Hex("The quick brown fox jumps over the lazy dog") ==
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("sha1 known vector") {
  Bytes d = sha1(std::string("abc"));
  std::string hex;
  static const char* h = "0123456789abcdef";
  for (uint8_t b : d) { hex += h[b >> 4]; hex += h[b & 15]; }
  CHECK(hex == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("hmac-sha256 known vector") {
  Bytes d = hmacSha256(std::string("key"),
                       std::string("The quick brown fox jumps over the lazy dog"));
  std::string hex;
  static const char* h = "0123456789abcdef";
  for (uint8_t b : d) { hex += h[b >> 4]; hex += h[b & 15]; }
  CHECK(hex ==
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST_CASE("pbkdf2-hmac-sha256 known vectors") {
  auto hex = [](const Bytes& b) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (uint8_t c : b) { o += h[c >> 4]; o += h[c & 15]; }
    return o;
  };
  Bytes salt{'s', 'a', 'l', 't'};
  CHECK(hex(pbkdf2Sha256("password", salt, 1, 32)) ==
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  CHECK(hex(pbkdf2Sha256("password", salt, 2, 32)) ==
        "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
  CHECK(hex(pbkdf2Sha256("password", salt, 4096, 32)) ==
        "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST_CASE("password hash round-trips and rejects wrong passwords") {
  std::string stored = passwordHash("correct horse battery staple", 1000);
  CHECK(stored.rfind("pbkdf2_sha256$", 0) == 0);
  CHECK(passwordVerify("correct horse battery staple", stored));
  CHECK_FALSE(passwordVerify("wrong", stored));
  CHECK_FALSE(passwordVerify("", stored));
  // A different salt produces a different digest.
  CHECK(passwordHash("same", 1000) != passwordHash("same", 1000));
}

TEST_CASE("base64 round-trip and url-safe variant") {
  std::string raw("\x00\x01\x02\xffhello", 8);
  std::string enc = base64Encode(raw);
  std::string dec;
  REQUIRE(base64Decode(enc, &dec));
  CHECK(dec == raw);
  std::string url = base64UrlEncode(std::string("\xfb\xff", 2));
  CHECK(url.find('+') == std::string::npos);
  CHECK(url.find('/') == std::string::npos);
}

TEST_CASE("url decode and query escaping") {
  CHECK(urlDecode("a%20b%2Bc") == "a b+c");
  CHECK(urlDecode("a+b") == "a b");
  std::string e = queryEscape("a b&c=d");
  CHECK(e == "a%20b%26c%3Dd");
  CHECK(urlDecode(e) == "a b&c=d");
}

TEST_CASE("random tokens are unique and correctly sized") {
  std::string a = randomBase64Url(32);
  std::string b = randomBase64Url(32);
  CHECK(a.size() >= 42);
  CHECK(a != b);
  CHECK(randomHex(8).size() == 16);
}

TEST_CASE("iso timestamps are UTC with millis") {
  std::string s = isoFromMs(0);
  CHECK(s == "1970-01-01T00:00:00.000Z");
  std::string n = isoNow();
  CHECK(n.size() == 24);
  CHECK(n.back() == 'Z');
}
