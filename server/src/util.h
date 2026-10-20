#pragma once
// util.h — small, dependency-free helpers shared by the server modules.
#include <cstdint>
#include <string>
#include <vector>

namespace dw {

std::string trim(const std::string& s);
std::string toLower(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& parts, const std::string& sep);

std::string urlDecode(const std::string& s);
std::string urlEncode(const std::string& s);

bool parseInt(const std::string& s, int* out);
bool parseInt64(const std::string& s, int64_t* out);

// Base64 (standard and URL-safe, no padding for the URL variant).
std::string base64Encode(const unsigned char* data, size_t len);
std::string base64Encode(const std::string& data);
std::string base64Encode(const std::vector<unsigned char>& data);
bool base64Decode(const std::string& in, std::string* out);
std::string base64UrlEncode(const unsigned char* data, size_t len);
std::string base64UrlEncode(const std::string& data);

// Cryptographically strong bytes from the OS. Returns false only if the OS
// source is unavailable.
bool randomBytes(void* buf, size_t len);
std::string randomHex(size_t bytes);
std::string randomBase64Url(size_t bytes);

// Clock.
int64_t nowMs();
std::string isoNow();                 // 2026-10-20T12:34:56.789Z
std::string isoFromMs(int64_t ms);

// RFC 3986-ish percent encoding for query values.
std::string queryEscape(const std::string& s);

}  // namespace dw
