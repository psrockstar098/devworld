#pragma once
// ratelimit.h — fixed-window rate limiter for auth endpoints.
//
// One limiter instance guards one dimension (per-IP or per-account). Buckets
// live in memory, so limits are per-process; a multi-process deployment
// would need shared state. The clock is injectable for tests; production
// uses the steady clock.
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace dw {

class RateLimiter {
 public:
  using Clock = std::function<int64_t()>;  // millis since an arbitrary epoch
  RateLimiter(int64_t windowMs, int maxPerWindow, Clock clock = nullptr);
  // Returns true when the attempt is allowed. When denied, retryAfterSec is
  // set to whole seconds until the window resets.
  bool allow(const std::string& key, int* retryAfterSec);
  void reset();

 private:
  struct Bucket {
    int64_t windowStart = 0;
    int count = 0;
  };
  int64_t windowMs_;
  int max_;
  Clock clock_;
  std::map<std::string, Bucket> buckets_;
};

// "ip:port" -> "ip". Bracketed IPv6 "[::1]:port" -> "::1". A peer without a
// port, or an unbracketed IPv6 address, is returned unchanged.
std::string clientIpFromPeer(const std::string& peer);

}  // namespace dw
