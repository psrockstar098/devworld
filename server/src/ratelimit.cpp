#include "ratelimit.h"

#include <chrono>
#include <utility>

namespace dw {
namespace {

int64_t steadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

RateLimiter::RateLimiter(int64_t windowMs, int maxPerWindow, Clock clock)
    : windowMs_(windowMs),
      max_(maxPerWindow),
      clock_(clock ? std::move(clock) : steadyNowMs) {}

bool RateLimiter::allow(const std::string& key, int* retryAfterSec) {
  int64_t now = clock_();
  Bucket& b = buckets_[key];
  if (now - b.windowStart >= windowMs_) {
    b.windowStart = now;
    b.count = 0;
  }
  if (b.count >= max_) {
    if (retryAfterSec) {
      int64_t left = b.windowStart + windowMs_ - now;
      *retryAfterSec = (int)((left + 999) / 1000);
      if (*retryAfterSec < 1) *retryAfterSec = 1;
    }
    return false;
  }
  ++b.count;
  return true;
}

void RateLimiter::reset() { buckets_.clear(); }

std::string clientIpFromPeer(const std::string& peer) {
  if (peer.empty()) return "";
  if (peer[0] == '[') {
    size_t close = peer.find(']');
    return close == std::string::npos ? peer : peer.substr(1, close - 1);
  }
  size_t colon = peer.rfind(':');
  if (colon == std::string::npos) return peer;
  // Exactly one colon means IPv4 "ip:port". More colons means unbracketed
  // IPv6, where the port cannot be told apart — keep the whole string.
  if (peer.find(':') == colon) return peer.substr(0, colon);
  return peer;
}

}  // namespace dw
