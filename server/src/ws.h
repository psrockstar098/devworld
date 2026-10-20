#pragma once
// ws.h — RFC 6455 WebSocket handshake, framing, and an in-process hub used to
// push live progression events to connected clients.
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "http.h"
#include "net.h"

namespace dw {

std::string wsAcceptKey(const std::string& clientKey);

// Returns 101 with the handshake headers, or 400 when the request is not a
// well-formed upgrade.
HttpResponse wsHandshake(const HttpRequest& req);

// Frame I/O. Server frames are unmasked; client frames must be masked.
bool wsSendFrame(int fd, int opcode, const std::string& payload);
bool wsSendText(int fd, const std::string& payload);

// Returns 1 on a frame, 0 on close/EOF, -1 on error.
int wsReadFrame(Socket& sock, int* opcode, std::string* payload);

struct WsConn {
  int fd = -1;
  std::string playerId;
  std::mutex writeMu;
};

class WsHub {
 public:
  std::shared_ptr<WsConn> add(const std::string& playerId, int fd);
  void remove(int fd);
  bool send(const std::shared_ptr<WsConn>& conn, const std::string& payload);
  void sendTo(const std::string& playerId, const std::string& payload);
  void broadcast(const std::string& payload);
  // How many live sockets this player has (tabs). Used to decide when a player
  // has really left the world, rather than just closing one tab.
  size_t countPlayer(const std::string& playerId) const;
  size_t size() const;

 private:
  mutable std::mutex mu_;
  std::map<int, std::shared_ptr<WsConn>> conns_;
  std::map<std::string, std::set<int>> byPlayer_;
};

}  // namespace dw
