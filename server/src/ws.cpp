#include "ws.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>

#include "crypto.h"
#include "json.h"
#include "util.h"

namespace dw {
namespace {

constexpr const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string lower(std::string s) { return toLower(std::move(s)); }

}  // namespace

std::string wsAcceptKey(const std::string& clientKey) {
  Bytes digest = sha1(clientKey + kGuid);
  return base64Encode(digest.data(), digest.size());
}

HttpResponse wsHandshake(const HttpRequest& req) {
  if (lower(req.header("upgrade")) != "websocket") {
    return HttpResponse::error(400, "expected websocket upgrade");
  }
  std::string key = req.header("sec-websocket-key");
  if (key.empty()) return HttpResponse::error(400, "missing Sec-WebSocket-Key");
  if (lower(req.header("sec-websocket-version")) != "13")
    return HttpResponse::error(400, "unsupported websocket version");

  HttpResponse r;
  r.status = 101;
  r.headers["Upgrade"] = "websocket";
  r.headers["Connection"] = "Upgrade";
  r.headers["Sec-WebSocket-Accept"] = wsAcceptKey(key);
  return r;
}

bool wsSendFrame(int fd, int opcode, const std::string& payload) {
  if (fd < 0) return false;
  std::string frame;
  frame.reserve(payload.size() + 10);
  frame += (char)(0x80 | (opcode & 0x0f));
  size_t n = payload.size();
  if (n < 126) {
    frame += (char)n;
  } else if (n <= 0xffff) {
    frame += (char)126;
    frame += (char)((n >> 8) & 0xff);
    frame += (char)(n & 0xff);
  } else {
    frame += (char)127;
    for (int i = 7; i >= 0; --i) frame += (char)((n >> (i * 8)) & 0xff);
  }
  frame += payload;
  // Write with MSG_NOSIGNAL directly; the fd is borrowed from the connection.
  const char* p = frame.data();
  size_t sent = 0;
  while (sent < frame.size()) {
    ssize_t w = ::send(fd, p + sent, frame.size() - sent, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (w == 0) return false;
    sent += (size_t)w;
  }
  return true;
}

bool wsSendText(int fd, const std::string& payload) {
  return wsSendFrame(fd, 0x1, payload);
}

int wsReadFrame(Socket& sock, int* opcode, std::string* payload) {
  std::string assembled;
  int firstOp = -1;
  bool fin = false;
  do {
    unsigned char hdr[2];
    if (!sock.readExact(hdr, 2)) return 0;
    fin = (hdr[0] & 0x80) != 0;
    int op = hdr[0] & 0x0f;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7f;

    if (len == 126) {
      unsigned char ext[2];
      if (!sock.readExact(ext, 2)) return 0;
      len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
      unsigned char ext[8];
      if (!sock.readExact(ext, 8)) return 0;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }
    if (len > 16 * 1024 * 1024) return -1;  // refuse oversized frames

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked && !sock.readExact(mask, 4)) return 0;

    std::string data((size_t)len, '\0');
    if (len && !sock.readExact(&data[0], (size_t)len)) return 0;
    if (masked) {
      for (size_t i = 0; i < data.size(); ++i) data[i] ^= (char)mask[i % 4];
    }

    if (op >= 0x8) {  // control frame: deliver immediately
      if (opcode) *opcode = op;
      if (payload) *payload = std::move(data);
      return 1;
    }
    if (firstOp < 0) firstOp = op;
    assembled += data;
  } while (!fin);

  if (opcode) *opcode = firstOp;
  if (payload) *payload = std::move(assembled);
  return 1;
}

std::shared_ptr<WsConn> WsHub::add(const std::string& playerId, int fd) {
  auto conn = std::make_shared<WsConn>();
  conn->playerId = playerId;
  conn->fd = fd;
  std::lock_guard<std::mutex> lock(mu_);
  conns_[fd] = conn;
  byPlayer_[playerId].insert(fd);
  return conn;
}

void WsHub::remove(int fd) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = conns_.find(fd);
  if (it == conns_.end()) return;
  auto pit = byPlayer_.find(it->second->playerId);
  if (pit != byPlayer_.end()) {
    pit->second.erase(fd);
    if (pit->second.empty()) byPlayer_.erase(pit);
  }
  conns_.erase(it);
}

bool WsHub::send(const std::shared_ptr<WsConn>& conn, const std::string& payload) {
  if (!conn) return false;
  std::lock_guard<std::mutex> lock(conn->writeMu);
  return wsSendText(conn->fd, payload);
}

void WsHub::sendTo(const std::string& playerId, const std::string& payload) {
  std::vector<std::shared_ptr<WsConn>> targets;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = byPlayer_.find(playerId);
    if (it == byPlayer_.end()) return;
    for (int fd : it->second) {
      auto c = conns_.find(fd);
      if (c != conns_.end()) targets.push_back(c->second);
    }
  }
  for (const auto& c : targets) send(c, payload);
}

void WsHub::broadcast(const std::string& payload) {
  std::vector<std::shared_ptr<WsConn>> targets;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& kv : conns_) targets.push_back(kv.second);
  }
  for (const auto& c : targets) send(c, payload);
}

size_t WsHub::countPlayer(const std::string& playerId) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = byPlayer_.find(playerId);
  return it == byPlayer_.end() ? 0 : it->second.size();
}

size_t WsHub::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return conns_.size();
}

}  // namespace dw
