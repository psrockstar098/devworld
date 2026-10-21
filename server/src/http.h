#pragma once
// http.h — a small HTTP/1.1 server and client built directly on the sockets in
// net.h. Enough for a JSON API, keep-alive, and the WebSocket upgrade path.
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net.h"

namespace dw {

struct HttpRequest {
  std::string method;
  std::string target;   // raw request target (path + query)
  std::string path;
  std::string query;    // raw query string, without '?'
  std::string version;
  std::map<std::string, std::string> headers;  // lower-case keys
  std::map<std::string, std::string> params;   // decoded query params
  std::string body;
  std::string peer;  // "ip:port" of the TCP peer, empty when unknown (unit tests)

  std::string header(const std::string& name,
                     const std::string& fallback = "") const;
  std::string param(const std::string& name, const std::string& fallback = "") const;
  bool hasHeader(const std::string& name) const;
};

struct HttpResponse {
  int status = 200;
  std::map<std::string, std::string> headers;
  std::string body;

  static HttpResponse json(std::string body, int status = 200);
  static HttpResponse text(std::string body, int status = 200,
                           const std::string& contentType = "text/plain; charset=utf-8");
  static HttpResponse error(int status, const std::string& message);
};

std::string httpStatusText(int status);

// Parses one request from `buffer` (which is retained across calls for
// keep-alive). Returns false on EOF/parse error; `err` distinguishes them.
bool readHttpRequest(Socket& sock, std::string* buffer, HttpRequest* req,
                     std::string* err, size_t maxBody = 2 * 1024 * 1024);

std::string serializeHttpResponse(const HttpResponse& res, bool keepAlive,
                                  bool headOnly = false);

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;
using UpgradeHandler = std::function<void(Socket&, const HttpRequest&)>;

class HttpServer {
 public:
  HttpServer(std::string host, int port, HttpHandler handler,
             UpgradeHandler upgrade = nullptr);
  ~HttpServer();

  bool start(std::string* err);
  void stop();
  int port() const { return port_; }
  bool running() const { return running_; }

 private:
  void acceptLoop();
  void serveConnection(Socket sock, std::string peer);
  void track(int fd);
  void untrack(int fd);

  std::string host_;
  int port_;
  HttpHandler handler_;
  UpgradeHandler upgrade_;
  Socket listener_;
  bool running_ = false;
  std::thread acceptThread_;
  std::vector<std::thread> workers_;
  std::mutex activeMu_;
  std::set<int> activeFds_;
};

// ---- client ---------------------------------------------------------------
struct ParsedUrl {
  std::string scheme = "http";
  std::string host;
  int port = 80;
  std::string path = "/";
  std::string query;
};

bool parseHttpUrl(const std::string& url, ParsedUrl* out, std::string* err);

struct HttpClientResponse {
  int status = 0;
  std::map<std::string, std::string> headers;
  std::string body;
};

bool httpRequest(const std::string& url, const std::string& method,
                 const std::map<std::string, std::string>& headers,
                 const std::string& body, HttpClientResponse* out,
                 std::string* err, int timeoutMs = 5000);

}  // namespace dw
