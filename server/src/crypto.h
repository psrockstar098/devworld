#pragma once
// crypto.h — the small cryptographic core the server needs, implemented in
// tree (no OpenSSL dependency): SHA-256, SHA-1 (WebSocket handshake),
// HMAC-SHA256 and PBKDF2-HMAC-SHA256 for password storage.
#include <cstdint>
#include <string>
#include <vector>

namespace dw {

using Bytes = std::vector<uint8_t>;

Bytes sha256(const void* data, size_t len);
Bytes sha256(const std::string& s);
inline Bytes sha256(const Bytes& b) {
  return sha256(b.empty() ? (const void*)"" : (const void*)b.data(), b.size());
}
std::string sha256Hex(const std::string& s);

Bytes sha1(const void* data, size_t len);
Bytes sha1(const std::string& s);

Bytes hmacSha256(const Bytes& key, const Bytes& msg);
Bytes hmacSha256(const std::string& key, const std::string& msg);

Bytes pbkdf2Sha256(const std::string& password, const Bytes& salt,
                   uint32_t iterations, size_t dkLen);

bool constantTimeEquals(const std::string& a, const std::string& b);
bool constantTimeEquals(const Bytes& a, const Bytes& b);

// Password format: pbkdf2_sha256$<iterations>$<salt-b64>$<hash-b64>
std::string passwordHash(const std::string& password,
                         uint32_t iterations = 120000);
bool passwordVerify(const std::string& password, const std::string& stored);

}  // namespace dw
