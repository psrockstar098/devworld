#pragma once
// net.h — RAII POSIX TCP sockets. The server intentionally carries no
// third-party networking library: a listening socket, blocking worker sockets,
// and bounded connect/read/write timeouts are enough for the whole API.
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

namespace dw {

class Socket {
 public:
  Socket() = default;
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  bool valid() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  void close();

  ssize_t readSome(void* buf, size_t len);
  bool readExact(void* buf, size_t len);
  bool writeAll(const void* buf, size_t len);

  bool setTimeouts(int recvMs, int sendMs);
  bool setNoDelay();

  // Accept one connection (blocking). `peer` receives "ip:port" when non-null.
  Socket acceptTcp(std::string* peer, std::string* err);

  // Listen on `host` (empty = all interfaces) and `port`.
  static Socket listenTcp(const std::string& host, int port, int backlog,
                          std::string* err);
  // Connect with a bounded timeout. Returns an invalid socket on failure.
  static Socket connectTcp(const std::string& host, int port, int timeoutMs,
                           std::string* err);

  std::string peerAddress() const;

 private:
  int fd_ = -1;
};

}  // namespace dw
