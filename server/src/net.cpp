#include "net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace dw {

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

void Socket::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

ssize_t Socket::readSome(void* buf, size_t len) {
  if (fd_ < 0) return -1;
  for (;;) {
    ssize_t n = ::recv(fd_, buf, len, 0);
    if (n < 0 && errno == EINTR) continue;
    return n;
  }
}

bool Socket::readExact(void* buf, size_t len) {
  unsigned char* p = (unsigned char*)buf;
  size_t got = 0;
  while (got < len) {
    ssize_t n = readSome(p + got, len - got);
    if (n <= 0) return false;
    got += (size_t)n;
  }
  return true;
}

bool Socket::writeAll(const void* buf, size_t len) {
  const unsigned char* p = (const unsigned char*)buf;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = ::send(fd_, p + sent, len - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += (size_t)n;
  }
  return true;
}

bool Socket::setTimeouts(int recvMs, int sendMs) {
  if (fd_ < 0) return false;
  bool ok = true;
  if (recvMs > 0) {
    timeval tv{};
    tv.tv_sec = recvMs / 1000;
    tv.tv_usec = (recvMs % 1000) * 1000;
    ok = ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0 && ok;
  }
  if (sendMs > 0) {
    timeval tv{};
    tv.tv_sec = sendMs / 1000;
    tv.tv_usec = (sendMs % 1000) * 1000;
    ok = ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) == 0 && ok;
  }
  return ok;
}

bool Socket::setNoDelay() {
  int one = 1;
  return ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0;
}

static void setErr(std::string* err, const std::string& msg) {
  if (err) *err = msg;
}

Socket Socket::listenTcp(const std::string& host, int port, int backlog,
                         std::string* err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* res = nullptr;
  std::string service = std::to_string(port);
  int rc = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(),
                         &hints, &res);
  if (rc != 0) {
    setErr(err, std::string("getaddrinfo: ") + gai_strerror(rc));
    return Socket();
  }
  Socket out;
  std::string last = "no address";
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
    if (fd < 0) { last = std::strerror(errno); continue; }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (::bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
        ::listen(fd, backlog) == 0) {
      out = Socket(fd);
      break;
    }
    last = std::strerror(errno);
    ::close(fd);
  }
  ::freeaddrinfo(res);
  if (!out.valid()) setErr(err, "listen " + host + ":" + service + ": " + last);
  return out;
}

Socket Socket::connectTcp(const std::string& host, int port, int timeoutMs,
                          std::string* err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  std::string service = std::to_string(port);
  int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &res);
  if (rc != 0) {
    setErr(err, std::string("getaddrinfo: ") + gai_strerror(rc));
    return Socket();
  }
  std::string last = "connection failed";
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
    if (fd < 0) { last = std::strerror(errno); continue; }
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int c = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (c < 0 && errno != EINPROGRESS) {
      last = std::strerror(errno);
      ::close(fd);
      continue;
    }
    if (c < 0) {
      pollfd pfd{fd, POLLOUT, 0};
      int pr = ::poll(&pfd, 1, timeoutMs);
      if (pr <= 0) {
        last = pr == 0 ? "connect timeout" : std::strerror(errno);
        ::close(fd);
        continue;
      }
      int soerr = 0;
      socklen_t slen = sizeof soerr;
      ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
      if (soerr != 0) {
        last = std::strerror(soerr);
        ::close(fd);
        continue;
      }
    }
    ::fcntl(fd, F_SETFL, flags);
    Socket out(fd);
    out.setNoDelay();
    if (timeoutMs > 0) out.setTimeouts(timeoutMs, timeoutMs);
    ::freeaddrinfo(res);
    return out;
  }
  ::freeaddrinfo(res);
  setErr(err, "connect " + host + ":" + service + ": " + last);
  return Socket();
}

Socket Socket::acceptTcp(std::string* peer, std::string* err) {
  sockaddr_storage addr{};
  socklen_t alen = sizeof addr;
  for (;;) {
    int fd = ::accept4(fd_, (sockaddr*)&addr, &alen, SOCK_CLOEXEC);
    if (fd < 0) {
      if (errno == EINTR) continue;
      setErr(err, std::string("accept: ") + std::strerror(errno));
      return Socket();
    }
    Socket out(fd);
    out.setNoDelay();
    if (peer) {
      char host[NI_MAXHOST] = {0};
      char serv[NI_MAXSERV] = {0};
      if (::getnameinfo((sockaddr*)&addr, alen, host, sizeof host, serv,
                        sizeof serv, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        *peer = std::string(host) + ":" + serv;
      } else {
        *peer = "?";
      }
    }
    return out;
  }
}

std::string Socket::peerAddress() const {
  sockaddr_storage addr{};
  socklen_t alen = sizeof addr;
  if (::getpeername(fd_, (sockaddr*)&addr, &alen) != 0) return "?";
  char host[NI_MAXHOST] = {0}, serv[NI_MAXSERV] = {0};
  if (::getnameinfo((sockaddr*)&addr, alen, host, sizeof host, serv, sizeof serv,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    return "?";
  return std::string(host) + ":" + serv;
}

}  // namespace dw
