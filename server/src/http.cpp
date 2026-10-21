#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "json.h"
#include "util.h"

namespace dw {
namespace {

std::string lower(std::string s) { return toLower(std::move(s)); }

bool readMore(Socket& sock, std::string* buffer, std::string* err) {
  char tmp[8192];
  ssize_t n = sock.readSome(tmp, sizeof tmp);
  if (n == 0) {
    if (err) *err = "eof";
    return false;
  }
  if (n < 0) {
    if (err) *err = "read error";
    return false;
  }
  buffer->append(tmp, (size_t)n);
  return true;
}

std::string decodeParam(const std::string& raw) { return urlDecode(raw); }

void parseQuery(const std::string& query, std::map<std::string, std::string>* out) {
  if (query.empty()) return;
  for (const auto& pair : split(query, '&')) {
    if (pair.empty()) continue;
    size_t eq = pair.find('=');
    std::string k = eq == std::string::npos ? pair : pair.substr(0, eq);
    std::string v = eq == std::string::npos ? "" : pair.substr(eq + 1);
    (*out)[decodeParam(k)] = decodeParam(v);
  }
}

}  // namespace

std::string HttpRequest::header(const std::string& name,
                                const std::string& fallback) const {
  auto it = headers.find(lower(name));
  return it == headers.end() ? fallback : it->second;
}

bool HttpRequest::hasHeader(const std::string& name) const {
  return headers.count(lower(name)) != 0;
}

std::string HttpRequest::param(const std::string& name,
                               const std::string& fallback) const {
  auto it = params.find(name);
  return it == params.end() ? fallback : it->second;
}

HttpResponse HttpResponse::json(std::string body, int status) {
  HttpResponse r;
  r.status = status;
  r.headers["content-type"] = "application/json";
  r.body = std::move(body);
  return r;
}

HttpResponse HttpResponse::text(std::string body, int status,
                                const std::string& contentType) {
  HttpResponse r;
  r.status = status;
  r.headers["content-type"] = contentType;
  r.body = std::move(body);
  return r;
}

HttpResponse HttpResponse::error(int status, const std::string& message) {
  JVal v = jobj();
  jset(v, "error", jstr(message));
  return json(dumpJson(v), status);
}

std::string httpStatusText(int status) {
  switch (status) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 422: return "Unprocessable Entity";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "OK";
  }
}

bool readHttpRequest(Socket& sock, std::string* buffer, HttpRequest* req,
                     std::string* err, size_t maxBody) {
  // Read until the header terminator.
  size_t headerEnd = std::string::npos;
  for (;;) {
    headerEnd = buffer->find("\r\n\r\n");
    if (headerEnd != std::string::npos) break;
    // Tolerate bare-LF clients in tests/tools.
    headerEnd = buffer->find("\n\n");
    if (headerEnd != std::string::npos) break;
    if (buffer->size() > 64 * 1024) {
      if (err) *err = "headers too large";
      return false;
    }
    if (!readMore(sock, buffer, err)) return false;
  }

  size_t terminator = 4;
  if (headerEnd >= 1 && (*buffer)[headerEnd] == '\n') terminator = 2;
  std::string head = buffer->substr(0, headerEnd);
  size_t consumed = headerEnd + terminator;

  std::vector<std::string> lines = split(head, '\n');
  for (auto& l : lines) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
  }
  if (lines.empty() || lines[0].empty()) {
    if (err) *err = "empty request";
    return false;
  }

  // Request line.
  std::istringstream rl(lines[0]);
  req->method.clear();
  req->target.clear();
  req->version.clear();
  rl >> req->method >> req->target >> req->version;
  if (req->method.empty() || req->target.empty()) {
    if (err) *err = "bad request line";
    return false;
  }
  if (req->version.empty()) req->version = "HTTP/1.0";

  // Headers.
  req->headers.clear();
  for (size_t i = 1; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = lower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));
    if (name.empty()) continue;
    auto it = req->headers.find(name);
    if (it == req->headers.end()) req->headers[name] = value;
    else it->second += ", " + value;
  }

  // Target → path + query.
  std::string target = req->target;
  size_t q = target.find('?');
  req->path = q == std::string::npos ? target : target.substr(0, q);
  req->query = q == std::string::npos ? "" : target.substr(q + 1);
  req->params.clear();
  parseQuery(req->query, &req->params);
  if (req->path.empty()) req->path = "/";

  // Body.
  req->body.clear();
  std::string expect = lower(req->header("expect"));
  if (expect.find("100-continue") != std::string::npos) {
    static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
    sock.writeAll(kContinue, sizeof kContinue - 1);
  }

  std::string te = lower(req->header("transfer-encoding"));
  if (te.find("chunked") != std::string::npos) {
    if (err) *err = "chunked bodies are not supported";
    return false;
  }

  size_t contentLength = 0;
  std::string cl = req->header("content-length");
  if (!cl.empty()) {
    int64_t n = 0;
    if (!parseInt64(cl, &n) || n < 0) {
      if (err) *err = "bad content-length";
      return false;
    }
    if ((size_t)n > maxBody) {
      if (err) *err = "body too large";
      return false;
    }
    contentLength = (size_t)n;
  }

  buffer->erase(0, consumed);
  while (buffer->size() < contentLength) {
    if (!readMore(sock, buffer, err)) return false;
  }
  req->body = buffer->substr(0, contentLength);
  buffer->erase(0, contentLength);
  return true;
}

std::string serializeHttpResponse(const HttpResponse& res, bool keepAlive,
                                  bool headOnly) {
  std::string out;
  out.reserve(128 + res.body.size());
  out += "HTTP/1.1 ";
  out += std::to_string(res.status);
  out += ' ';
  out += httpStatusText(res.status);
  out += "\r\n";

  // 101 Switching Protocols is not an HTTP message with a body: emit exactly
  // the upgrade headers (Connection: Upgrade must survive) and stop.
  if (res.status == 101) {
    for (const auto& h : res.headers) {
      out += h.first;
      out += ": ";
      out += h.second;
      out += "\r\n";
    }
    out += "\r\n";
    return out;
  }

  bool hasType = false;
  for (const auto& h : res.headers) {
    std::string k = lower(h.first);
    if (k == "content-length" || k == "connection" || k == "transfer-encoding")
      continue;
    if (k == "content-type") hasType = true;
    out += h.first;
    out += ": ";
    out += h.second;
    out += "\r\n";
  }
  if (!hasType) out += "Content-Type: application/json\r\n";
  out += "Content-Length: ";
  out += std::to_string(res.body.size());
  out += "\r\n";
  out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
  out += "\r\n";
  if (!headOnly) out += res.body;
  return out;
}

HttpServer::HttpServer(std::string host, int port, HttpHandler handler,
                       UpgradeHandler upgrade)
    : host_(std::move(host)), port_(port), handler_(std::move(handler)),
      upgrade_(std::move(upgrade)) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(std::string* err) {
  listener_ = Socket::listenTcp(host_, port_, 128, err);
  if (!listener_.valid()) return false;
  // If port 0 was requested, discover the kernel-assigned port.
  if (port_ == 0) {
    sockaddr_storage addr{};
    socklen_t len = sizeof addr;
    if (::getsockname(listener_.fd(), (sockaddr*)&addr, &len) == 0) {
      if (addr.ss_family == AF_INET)
        port_ = ntohs(((sockaddr_in*)&addr)->sin_port);
      else if (addr.ss_family == AF_INET6)
        port_ = ntohs(((sockaddr_in6*)&addr)->sin6_port);
    }
  }
  running_ = true;
  acceptThread_ = std::thread([this] { acceptLoop(); });
  return true;
}

void HttpServer::stop() {
  if (!running_) return;
  running_ = false;
  // Wake the blocking accept(), then close the listener.
  {
    std::string e;
    Socket wake = Socket::connectTcp("127.0.0.1", port_, 200, &e);
    wake.close();
  }
  listener_.close();
  {
    std::lock_guard<std::mutex> lock(activeMu_);
    for (int fd : activeFds_) ::shutdown(fd, SHUT_RDWR);
  }
  if (acceptThread_.joinable()) acceptThread_.join();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
}

void HttpServer::track(int fd) {
  std::lock_guard<std::mutex> lock(activeMu_);
  activeFds_.insert(fd);
}

void HttpServer::untrack(int fd) {
  std::lock_guard<std::mutex> lock(activeMu_);
  activeFds_.erase(fd);
}

void HttpServer::acceptLoop() {
  while (running_) {
    std::string peer;
    std::string err;
    Socket client = listener_.acceptTcp(&peer, &err);
    if (!client.valid()) {
      if (!running_) break;
      continue;
    }
    client.setTimeouts(30000, 30000);
    workers_.emplace_back([this, c = std::move(client), peer]() mutable {
      serveConnection(std::move(c), peer);
    });
  }
}

void HttpServer::serveConnection(Socket sock, std::string peer) {
  track(sock.fd());
  struct Guard {
    HttpServer* server;
    int fd;
    ~Guard() { server->untrack(fd); }
  } guard{this, sock.fd()};

  std::string buffer;
  for (;;) {
    HttpRequest req;
    std::string err;
    if (!readHttpRequest(sock, &buffer, &req, &err)) {
      if (err == "body too large" || err == "chunked bodies are not supported" ||
          err == "headers too large" || err == "bad content-length") {
        HttpResponse r = HttpResponse::error(413, err);
        std::string out = serializeHttpResponse(r, false);
        sock.writeAll(out.data(), out.size());
      }
      return;
    }

    req.peer = peer;
    HttpResponse res;
    try {
      res = handler_ ? handler_(req) : HttpResponse::error(404, "not found");
    } catch (const std::exception& e) {
      res = HttpResponse::error(500, "internal error");
    } catch (...) {
      res = HttpResponse::error(500, "internal error");
    }

    if (res.status == 101) {
      std::string out = serializeHttpResponse(res, true);
      if (!sock.writeAll(out.data(), out.size())) return;
      if (upgrade_) {
        try {
          upgrade_(sock, req);
        } catch (...) {
        }
      }
      return;
    }

    bool keepAlive = true;
    std::string conn = lower(req.header("connection"));
    if (req.version == "HTTP/1.0" && conn.find("keep-alive") == std::string::npos)
      keepAlive = false;
    if (conn.find("close") != std::string::npos) keepAlive = false;

    bool headOnly = req.method == "HEAD";
    std::string out = serializeHttpResponse(res, keepAlive, headOnly);
    if (!sock.writeAll(out.data(), out.size())) return;
    if (!keepAlive) return;
  }
}

// ---- client ---------------------------------------------------------------

bool parseHttpUrl(const std::string& url, ParsedUrl* out, std::string* err) {
  *out = ParsedUrl{};
  std::string rest = url;
  size_t schemePos = rest.find("://");
  if (schemePos != std::string::npos) {
    out->scheme = toLower(rest.substr(0, schemePos));
    rest = rest.substr(schemePos + 3);
  }
  if (out->scheme != "http") {
    if (err) *err = "only http:// is supported";
    return false;
  }
  size_t slash = rest.find('/');
  std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

  size_t q = path.find('?');
  if (q != std::string::npos) {
    out->query = path.substr(q + 1);
    path = path.substr(0, q);
  }
  out->path = path.empty() ? "/" : path;

  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    out->host = authority.substr(0, colon);
    int p = 0;
    if (!parseInt(authority.substr(colon + 1), &p) || p <= 0 || p > 65535) {
      if (err) *err = "bad port";
      return false;
    }
    out->port = p;
  } else {
    out->host = authority;
  }
  if (out->host.empty()) {
    if (err) *err = "missing host";
    return false;
  }
  return true;
}

bool httpRequest(const std::string& url, const std::string& method,
                 const std::map<std::string, std::string>& headers,
                 const std::string& requestBody, HttpClientResponse* out,
                 std::string* err, int timeoutMs) {
  if (err) err->clear();
  ParsedUrl u;
  if (!parseHttpUrl(url, &u, err)) return false;
  Socket sock = Socket::connectTcp(u.host, u.port, timeoutMs, err);
  if (!sock.valid()) return false;
  sock.setTimeouts(timeoutMs, timeoutMs);

  std::string target = u.path;
  if (!u.query.empty()) target += "?" + u.query;

  std::string req;
  req += method + " " + target + " HTTP/1.1\r\n";
  req += "Host: " + u.host + ":" + std::to_string(u.port) + "\r\n";
  bool hasLength = false;
  for (const auto& h : headers) {
    if (toLower(h.first) == "content-length") hasLength = true;
    req += h.first + ": " + h.second + "\r\n";
  }
  if (!hasLength) req += "Content-Length: " + std::to_string(requestBody.size()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  req += requestBody;

  if (!sock.writeAll(req.data(), req.size())) {
    if (err) *err = "write error";
    return false;
  }

  std::string raw;
  char tmp[8192];
  auto readMoreClient = [&]() -> bool {
    ssize_t n = sock.readSome(tmp, sizeof tmp);
    if (n == 0) return false;  // EOF
    if (n < 0) {
      if (err) *err = "read error";
      return false;
    }
    raw.append(tmp, (size_t)n);
    if (raw.size() > 64 * 1024 * 1024) {
      if (err) *err = "response too large";
      return false;
    }
    return true;
  };

  // Read until the end of the response headers.
  size_t headerEnd = std::string::npos;
  size_t term = 4;
  while (headerEnd == std::string::npos) {
    headerEnd = raw.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
      term = 4;
      break;
    }
    headerEnd = raw.find("\n\n");
    if (headerEnd != std::string::npos) {
      term = 2;
      break;
    }
    if (!readMoreClient()) {
      if (err && err->empty()) *err = "malformed response";
      return false;
    }
  }

  std::string head = raw.substr(0, headerEnd);
  std::string body = raw.substr(headerEnd + term);
  raw.clear();
  std::vector<std::string> lines = split(head, '\n');
  for (auto& l : lines)
    if (!l.empty() && l.back() == '\r') l.pop_back();
  if (lines.empty()) {
    if (err) *err = "empty response";
    return false;
  }
  std::istringstream sl(lines[0]);
  std::string version;
  sl >> version >> out->status;
  out->headers.clear();
  for (size_t i = 1; i < lines.size(); ++i) {
    size_t colon = lines[i].find(':');
    if (colon == std::string::npos) continue;
    out->headers[lower(trim(lines[i].substr(0, colon)))] =
        trim(lines[i].substr(colon + 1));
  }

  auto te = out->headers.find("transfer-encoding");
  if (te != out->headers.end() &&
      lower(te->second).find("chunked") != std::string::npos) {
    // Decode chunked framing.
    std::string decoded;
    for (;;) {
      size_t eol = body.find("\r\n");
      while (eol == std::string::npos) {
        if (!readMoreClient()) {
          if (err) *err = "truncated chunked response";
          return false;
        }
        body += raw;
        raw.clear();
        eol = body.find("\r\n");
      }
      std::string sizeLine = body.substr(0, eol);
      size_t semi = sizeLine.find(';');
      if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);
      char* end = nullptr;
      unsigned long n = std::strtoul(sizeLine.c_str(), &end, 16);
      if (!end || (*end != '\0' && !std::isspace((unsigned char)*end))) {
        if (err) *err = "bad chunk size";
        return false;
      }
      body.erase(0, eol + 2);
      if (n == 0) {
        // Consume optional trailers up to the terminating blank line.
        for (;;) {
          size_t t = body.find("\r\n");
          while (t == std::string::npos) {
            if (!readMoreClient()) break;
            body += raw;
            raw.clear();
            t = body.find("\r\n");
          }
          if (t == std::string::npos) break;
          if (t == 0) {
            body.erase(0, 2);
            break;
          }
          body.erase(0, t + 2);
        }
        break;
      }
      while (body.size() < n + 2) {
        if (!readMoreClient()) {
          if (err) *err = "truncated chunked response";
          return false;
        }
        body += raw;
        raw.clear();
      }
      decoded.append(body, 0, n);
      body.erase(0, n + 2);
    }
    out->body = std::move(decoded);
    return true;
  }

  auto cl = out->headers.find("content-length");
  if (cl != out->headers.end()) {
    int64_t n = 0;
    if (parseInt64(cl->second, &n) && n >= 0) {
      while ((int64_t)body.size() < n) {
        if (!readMoreClient()) {
          if (err) *err = "truncated response";
          return false;
        }
        body += raw;
        raw.clear();
      }
      body.resize((size_t)n);
    }
    out->body = std::move(body);
    return true;
  }

  // No framing: read to EOF (the request asked for Connection: close).
  for (;;) {
    if (!readMoreClient()) break;
    body += raw;
    raw.clear();
  }
  out->body = std::move(body);
  return true;
}

}  // namespace dw
