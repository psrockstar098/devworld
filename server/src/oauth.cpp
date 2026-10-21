#include "oauth.h"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "crypto.h"
#include "util.h"

namespace dw {
namespace {

std::string env(const char* name, const std::string& fallback = "") {
  const char* v = std::getenv(name);
  return v && *v ? std::string(v) : fallback;
}

int envInt(const char* name, int fallback) {
  const char* v = std::getenv(name);
  if (!v || !*v) return fallback;
  int n = 0;
  return parseInt(v, &n) && n > 0 ? n : fallback;
}

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

std::string lowerHost(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool validHeaderValue(const std::string& v) {
  for (unsigned char c : v)
    if (c == '\r' || c == '\n' || c == 0) return false;
  return true;
}

std::string appendQuery(const std::string& url, const std::string& key,
                        const std::string& value) {
  std::string out = url;
  if (out.find('?') == std::string::npos)
    out += '?';
  else if (!out.empty() && out.back() != '?' && out.back() != '&')
    out += '&';
  out += urlEncode(key);
  out += '=';
  out += urlEncode(value);
  return out;
}

std::string appendRawQuery(const std::string& url, const std::string& params) {
  if (params.empty()) return url;
  std::string out = url;
  if (out.find('?') == std::string::npos)
    out += '?';
  else if (!out.empty() && out.back() != '?' && out.back() != '&')
    out += '&';
  out += params;
  return out;
}

// scheme://host[:port], normalising case and default ports. Rejects userinfo.
bool originOf(const std::string& url, std::string* origin) {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return false;
  std::string scheme = toLower(url.substr(0, schemeEnd));
  if (scheme != "http" && scheme != "https") return false;
  size_t start = schemeEnd + 3;
  size_t end = url.find_first_of("/?#", start);
  std::string authority = url.substr(start, end == std::string::npos
                                              ? std::string::npos
                                              : end - start);
  if (authority.empty() || authority.find('@') != std::string::npos)
    return false;
  for (unsigned char c : authority)
    if (c <= 0x20 || c == '\\') return false;

  std::string host = authority;
  std::string port;
  if (!authority.empty() && authority[0] == '[') {
    size_t close = authority.find(']');
    if (close == std::string::npos) return false;
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') return false;
      port = authority.substr(close + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    }
  }
  if (host.empty()) return false;
  host = lowerHost(host);
  if (!port.empty()) {
    int p = 0;
    if (!parseInt(port, &p) || p <= 0 || p > 65535) return false;
    if (!((scheme == "http" && p == 80) || (scheme == "https" && p == 443)))
      host += ":" + std::to_string(p);
  }
  *origin = scheme + "://" + host;
  return true;
}

std::string scalarString(const JVal& v) {
  if (v.type == JVal::STR) return v.str;
  if (v.type == JVal::NUM) {
    if (v.num == (double)(long long)v.num)
      return std::to_string((long long)v.num);
    std::ostringstream os;
    os << v.num;
    return os.str();
  }
  return "";
}

std::string snippet(const std::string& body, size_t max = 240) {
  std::string s = body;
  for (char& c : s)
    if (c == '\n' || c == '\r') c = ' ';
  if (s.size() > max) s = s.substr(0, max) + "…";
  return s;
}

bool writeAllFd(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    off += (size_t)n;
  }
  return true;
}

// HTTPS via the system curl binary. The request body (which carries client
// secrets) goes over stdin, never argv. Redirects are not followed, and the
// protocol is pinned to https.
bool curlHttpsRequest(const std::string& url, const std::string& method,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body, HttpClientResponse* out,
                      std::string* err, int timeoutMs) {
  if (err) err->clear();
  int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1}, errPipe[2] = {-1, -1};
  auto closePipe = [](int p[2]) {
    if (p[0] >= 0) ::close(p[0]);
    if (p[1] >= 0) ::close(p[1]);
    p[0] = p[1] = -1;
  };
  if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
    closePipe(inPipe);
    closePipe(outPipe);
    closePipe(errPipe);
    if (err) *err = "could not create pipes for https request";
    return false;
  }

  int timeoutSec = timeoutMs > 0 ? (timeoutMs + 999) / 1000 : 15;
  bool sendsBody = !body.empty() || method == "POST" || method == "PUT" ||
                   method == "PATCH" || method == "DELETE";

  std::vector<std::string> args = {
      "curl", "-q", "-sS", "--proto", "=https", "--connect-timeout", "5",
      "--max-time", std::to_string(timeoutSec), "--max-filesize", "4194304",
      "-X", method, "-w", "\n%{http_code}"};
  for (const auto& h : headers) {
    if (!validHeaderValue(h.first) || !validHeaderValue(h.second)) {
      closePipe(inPipe);
      closePipe(outPipe);
      closePipe(errPipe);
      if (err) *err = "invalid request header";
      return false;
    }
    args.push_back("-H");
    args.push_back(h.first + ": " + h.second);
  }
  if (sendsBody) {
    args.push_back("--data-binary");
    args.push_back("@-");
  }
  args.push_back("--");
  args.push_back(url);

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    closePipe(inPipe);
    closePipe(outPipe);
    closePipe(errPipe);
    if (err) *err = "could not start curl";
    return false;
  }
  if (pid == 0) {
    ::dup2(inPipe[0], STDIN_FILENO);
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(errPipe[1], STDERR_FILENO);
    ::close(inPipe[0]);
    ::close(inPipe[1]);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[0]);
    ::close(errPipe[1]);
    ::execvp("curl", argv.data());
    ::_exit(127);
  }

  ::close(inPipe[0]);
  ::close(outPipe[1]);
  ::close(errPipe[1]);
  writeAllFd(inPipe[1], body);
  ::close(inPipe[1]);

  std::string stdoutData;
  std::string stderrData;
  bool outOpen = true, errOpen = true;
  bool timedOut = false;
  while (outOpen || errOpen) {
    struct pollfd fds[2];
    int n = 0;
    if (outOpen) {
      fds[n].fd = outPipe[0];
      fds[n].events = POLLIN;
      fds[n].revents = 0;
      ++n;
    }
    if (errOpen) {
      fds[n].fd = errPipe[0];
      fds[n].events = POLLIN;
      fds[n].revents = 0;
      ++n;
    }
    int rc = ::poll(fds, (nfds_t)n, timeoutMs);
    if (rc < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (rc == 0) {
      timedOut = true;
      break;
    }
    for (int i = 0; i < n; ++i) {
      if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      char buf[8192];
      for (;;) {
        ssize_t got = ::read(fds[i].fd, buf, sizeof buf);
        if (got > 0) {
          std::string* dst = fds[i].fd == outPipe[0] ? &stdoutData : &stderrData;
          dst->append(buf, (size_t)got);
          if (dst->size() > 8 * 1024 * 1024) {
            timedOut = true;
            break;
          }
          continue;
        }
        if (got < 0 && errno == EINTR) continue;
        if (fds[i].fd == outPipe[0])
          outOpen = false;
        else
          errOpen = false;
        break;
      }
      if (timedOut) break;
    }
    if (timedOut) break;
  }
  ::close(outPipe[0]);
  ::close(errPipe[0]);

  int wstatus = 0;
  if (timedOut) ::kill(pid, SIGKILL);
  while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {
  }

  if (timedOut) {
    if (err) *err = "https request timed out";
    return false;
  }
  if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 127) {
    if (err) *err = "curl is required for HTTPS OAuth requests";
    return false;
  }

  size_t nl = stdoutData.rfind('\n');
  if (nl == std::string::npos) {
    if (err) *err = stderrData.empty() ? "empty HTTPS response" : snippet(stderrData);
    return false;
  }
  std::string code = trim(stdoutData.substr(nl + 1));
  std::string responseBody = stdoutData.substr(0, nl);
  int status = 0;
  if (!parseInt(code, &status) || status <= 0) {
    if (err)
      *err = stderrData.empty() ? "could not read HTTP status" : snippet(stderrData);
    return false;
  }
  out->status = status;
  out->body = std::move(responseBody);
  out->headers.clear();
  return true;
}

OAuthProvider baseProvider(const std::string& id, const std::string& name,
                           const std::string& brand, OAuthProviderKind kind,
                           const std::string& authorizeUrl,
                           const std::string& tokenUrl,
                           const std::string& userInfoUrl,
                           const std::string& scope) {
  OAuthProvider p;
  p.id = id;
  p.name = name;
  p.brand = brand;
  p.kind = kind;
  p.authorizeUrl = authorizeUrl;
  p.tokenUrl = tokenUrl;
  p.userInfoUrl = userInfoUrl;
  p.scope = scope;
  return p;
}

void applyOverrides(const std::string& id, OAuthProvider* p) {
  std::string prefix = "OAUTH_" + upper(id) + "_";
  p->clientId = env((prefix + "CLIENT_ID").c_str(), p->clientId);
  p->clientSecret = env((prefix + "CLIENT_SECRET").c_str(), p->clientSecret);
  p->authorizeUrl = env((prefix + "AUTHORIZE_URL").c_str(), p->authorizeUrl);
  p->tokenUrl = env((prefix + "TOKEN_URL").c_str(), p->tokenUrl);
  p->userInfoUrl = env((prefix + "USERINFO_URL").c_str(), p->userInfoUrl);
  p->emailsUrl = env((prefix + "EMAILS_URL").c_str(), p->emailsUrl);
  p->scope = env((prefix + "SCOPE").c_str(), p->scope);
  p->name = env((prefix + "NAME").c_str(), p->name);
}

}  // namespace

std::string OAuthProvider::redirectUri(const std::string& baseUrl) const {
  std::string base = baseUrl;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base + "/auth/oauth/" + id + "/callback";
}

const OAuthProvider* OAuthConfig::provider(const std::string& id) const {
  for (const auto& p : providers)
    if (p.id == id) return &p;
  return nullptr;
}

bool OAuthConfig::returnToAllowed(const std::string& url,
                                  std::string* origin) const {
  if (url.empty() || url.size() > 2048) return false;
  for (unsigned char c : url)
    if (c < 0x20 || c == '\\') return false;
  std::string got;
  if (!originOf(url, &got)) return false;
  for (const auto& raw : split(allowedOrigins, ',')) {
    std::string candidate;
    if (originOf(trim(raw), &candidate) && candidate == got) {
      if (origin) *origin = got;
      return true;
    }
  }
  return false;
}

OAuthConfig OAuthConfig::fromEnv() {
  OAuthConfig c;
  c.baseUrl = env("OAUTH_BASE_URL", c.baseUrl);
  while (!c.baseUrl.empty() && c.baseUrl.back() == '/') c.baseUrl.pop_back();
  c.successRedirect = env("OAUTH_SUCCESS_REDIRECT", "");
  c.allowedOrigins = env("OAUTH_ALLOWED_ORIGINS", c.allowedOrigins);
  c.stateTtlSec = envInt("OAUTH_STATE_TTL_SEC", c.stateTtlSec);
  c.httpTimeoutMs = envInt("OAUTH_HTTP_TIMEOUT_MS", c.httpTimeoutMs);

  auto add = [&](OAuthProvider p) {
    applyOverrides(p.id, &p);
    if (p.configured()) c.providers.push_back(std::move(p));
  };

  add(baseProvider("google", "Google", "#4285f4", OAuthProviderKind::Google,
                   "https://accounts.google.com/o/oauth2/v2/auth",
                   "https://oauth2.googleapis.com/token",
                   "https://openidconnect.googleapis.com/v1/userinfo",
                   "openid email profile"));

  add(baseProvider("github", "GitHub", "#24292f", OAuthProviderKind::GitHub,
                   "https://github.com/login/oauth/authorize",
                   "https://github.com/login/oauth/access_token",
                   "https://api.github.com/user", "read:user user:email"));

  add(baseProvider("facebook", "Facebook", "#1877f2",
                   OAuthProviderKind::Facebook,
                   "https://www.facebook.com/dialog/oauth",
                   "https://graph.facebook.com/oauth/access_token",
                   "https://graph.facebook.com/me", "email,public_profile"));

  add(baseProvider("discord", "Discord", "#5865f2", OAuthProviderKind::Discord,
                   "https://discord.com/oauth2/authorize",
                   "https://discord.com/api/oauth2/token",
                   "https://discord.com/api/users/@me", "identify email"));

  add(baseProvider("gitlab", "GitLab", "#fc6d26", OAuthProviderKind::GitLab,
                   "https://gitlab.com/oauth/authorize",
                   "https://gitlab.com/oauth/token",
                   "https://gitlab.com/api/v4/user", "read_user"));

  add(baseProvider("microsoft", "Microsoft", "#0078d4",
                   OAuthProviderKind::Microsoft,
                   "https://login.microsoftonline.com/common/oauth2/v2.0/authorize",
                   "https://login.microsoftonline.com/common/oauth2/v2.0/token",
                   "https://graph.microsoft.com/v1.0/me",
                   "openid profile email User.Read"));

  // Any standards-compliant OIDC provider can be added with the OAUTH_OIDC_*
  // variables; no code change is needed.
  OAuthProvider oidc;
  oidc.id = "oidc";
  oidc.name = env("OAUTH_OIDC_NAME", "Single sign-on");
  oidc.brand = env("OAUTH_OIDC_BRAND", "#64748b");
  oidc.kind = OAuthProviderKind::OIDC;
  oidc.clientId = env("OAUTH_OIDC_CLIENT_ID", "");
  oidc.clientSecret = env("OAUTH_OIDC_CLIENT_SECRET", "");
  oidc.authorizeUrl = env("OAUTH_OIDC_AUTHORIZE_URL", "");
  oidc.tokenUrl = env("OAUTH_OIDC_TOKEN_URL", "");
  oidc.userInfoUrl = env("OAUTH_OIDC_USERINFO_URL", "");
  oidc.scope = env("OAUTH_OIDC_SCOPE", "openid email profile");
  if (oidc.configured()) c.providers.push_back(std::move(oidc));

  for (auto& p : c.providers) {
    // Google's consent screen should let the player pick the right account.
    if (p.id == "google")
      p.extraAuthorizeParams = "access_type=online&prompt=select_account";
    // These providers do not implement PKCE on the web authorization flow.
    if (p.id == "github" || p.id == "facebook") p.pkce = false;
  }
  return c;
}

bool oauthHttpRequest(const std::string& url, const std::string& method,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body, HttpClientResponse* out,
                      std::string* err, int timeoutMs) {
  if (startsWith(toLower(url), "http://"))
    return httpRequest(url, method, headers, body, out, err, timeoutMs);
  if (startsWith(toLower(url), "https://"))
    return curlHttpsRequest(url, method, headers, body, out, err, timeoutMs);
  if (err) *err = "unsupported OAuth URL scheme";
  return false;
}

bool parseOAuthBody(const std::string& body, JVal* out, std::string* err) {
  std::string text = trim(body);
  if (!text.empty() && (text[0] == '{' || text[0] == '[')) {
    try {
      *out = parseJson(text);
      return true;
    } catch (...) {
      if (err) *err = "invalid JSON from provider";
      return false;
    }
  }
  JVal v = jobj();
  for (const auto& pair : split(text, '&')) {
    if (pair.empty()) continue;
    size_t eq = pair.find('=');
    std::string k = eq == std::string::npos ? pair : pair.substr(0, eq);
    std::string val = eq == std::string::npos ? "" : pair.substr(eq + 1);
    jset(v, urlDecode(k), jstr(urlDecode(val)));
  }
  *out = std::move(v);
  return true;
}

bool OAuthService::start(const std::string& providerId,
                         const std::string& returnTo, bool json,
                         std::string* authorizeUrl, std::string* state,
                         int* status, std::string* err) {
  if (status) *status = 500;
  const OAuthProvider* p = cfg_.provider(providerId);
  if (!p || !p->configured()) {
    if (status) *status = 404;
    if (err) *err = "unknown or unconfigured sign-in provider";
    return false;
  }

  std::string target = returnTo;
  if (target.empty()) target = cfg_.successRedirect;
  if (!target.empty() && !cfg_.returnToAllowed(target, nullptr)) {
    if (status) *status = 400;
    if (err) *err = "returnTo is not an allowed origin";
    return false;
  }
  if (target.empty() && !json) {
    if (status) *status = 400;
    if (err) *err = "returnTo is required";
    return false;
  }

  StateRecord st;
  st.state = randomBase64Url(24);
  st.provider = p->id;
  st.verifier = p->pkce ? randomBase64Url(32) : "";
  st.returnTo = target;
  st.json = json;
  if (st.state.empty()) {
    if (status) *status = 500;
    if (err) *err = "could not generate OAuth state";
    return false;
  }

  JVal j = jobj();
  jset(j, "provider", jstr(st.provider));
  jset(j, "verifier", jstr(st.verifier));
  jset(j, "returnTo", jstr(st.returnTo));
  jset(j, "json", jbool(st.json));
  jset(j, "createdAt", jstr(isoNow()));
  if (!cache_->set(stateKey(st.state), dumpJson(j), cfg_.stateTtlSec, err)) {
    if (status) *status = 503;
    if (err) *err = "OAuth state store unavailable";
    return false;
  }

  std::string url = appendQuery(p->authorizeUrl, "response_type", "code");
  url = appendQuery(url, "client_id", p->clientId);
  url = appendQuery(url, "redirect_uri", p->redirectUri(cfg_.baseUrl));
  if (!p->scope.empty()) url = appendQuery(url, "scope", p->scope);
  url = appendQuery(url, "state", st.state);
  if (p->pkce) {
    Bytes digest = sha256(st.verifier);
    std::string challenge = base64UrlEncode(digest.data(), digest.size());
    url = appendQuery(url, "code_challenge", challenge);
    url = appendQuery(url, "code_challenge_method", "S256");
  }
  url = appendRawQuery(url, p->extraAuthorizeParams);

  if (authorizeUrl) *authorizeUrl = url;
  if (state) *state = st.state;
  if (status) *status = 200;
  return true;
}

bool OAuthService::loadState(const std::string& state, StateRecord* out,
                             bool* found, std::string* err) {
  *found = false;
  if (state.size() < 16 || state.size() > 128) return true;
  std::string raw;
  if (!cache_->get(stateKey(state), &raw, found, err)) return false;
  if (!*found) return true;
  JVal j;
  try {
    j = parseJson(raw);
  } catch (...) {
    if (err) *err = "corrupt OAuth state";
    return false;
  }
  out->state = state;
  out->provider = j.s("provider");
  out->verifier = j.s("verifier");
  out->returnTo = j.s("returnTo");
  out->json = j.boolean("json");
  return !out->provider.empty();
}

bool OAuthService::exchangeCode(const OAuthProvider& p, const std::string& code,
                                const std::string& verifier,
                                std::string* accessToken, std::string* err) {
  std::string form = "grant_type=authorization_code";
  form += "&code=" + urlEncode(code);
  form += "&client_id=" + urlEncode(p.clientId);
  form += "&client_secret=" + urlEncode(p.clientSecret);
  form += "&redirect_uri=" + urlEncode(p.redirectUri(cfg_.baseUrl));
  if (p.pkce && !verifier.empty())
    form += "&code_verifier=" + urlEncode(verifier);

  std::map<std::string, std::string> headers = {
      {"Content-Type", "application/x-www-form-urlencoded"},
      {"Accept", "application/json"},
      {"User-Agent", "devworld-server/0.2"},
  };
  HttpClientResponse res;
  if (!oauthHttpRequest(p.tokenUrl, "POST", headers, form, &res, err,
                        cfg_.httpTimeoutMs))
    return false;
  if (res.status < 200 || res.status >= 300) {
    if (err)
      *err = "token endpoint returned HTTP " + std::to_string(res.status) +
             ": " + snippet(res.body);
    return false;
  }
  JVal token;
  if (!parseOAuthBody(res.body, &token, err)) return false;
  std::string access = token.s("access_token");
  if (access.empty()) {
    std::string detail = token.s("error_description");
    if (detail.empty()) detail = token.s("error");
    if (detail.empty()) detail = snippet(res.body);
    if (err) *err = "provider did not return an access token: " + detail;
    return false;
  }
  if (access.size() > 4096) {
    if (err) *err = "provider returned an oversized access token";
    return false;
  }
  *accessToken = access;
  return true;
}

bool OAuthService::fetchIdentity(const OAuthProvider& p,
                                 const std::string& token, OAuthIdentity* out,
                                 std::string* err) {
  std::map<std::string, std::string> headers = {
      {"Authorization", "Bearer " + token},
      {"Accept", "application/json"},
      {"User-Agent", "devworld-server/0.2"},
  };
  HttpClientResponse res;
  if (!oauthHttpRequest(p.userInfoUrl, "GET", headers, "", &res, err,
                        cfg_.httpTimeoutMs))
    return false;
  if (res.status < 200 || res.status >= 300) {
    if (err)
      *err = "userinfo endpoint returned HTTP " + std::to_string(res.status) +
             ": " + snippet(res.body);
    return false;
  }
  JVal u;
  if (!parseOAuthBody(res.body, &u, err)) return false;

  OAuthIdentity id;
  id.provider = p.id;
  if (p.kind == OAuthProviderKind::Google) {
    id.subject = scalarString(u.at("sub"));
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    id.emailVerified = u.boolean("email_verified");
    id.displayName = u.s("name");
    if (id.displayName.empty()) id.displayName = u.s("given_name");
    id.avatarUrl = u.s("picture");
    id.login = id.email.empty() ? "" : id.email.substr(0, id.email.find('@'));
  } else if (p.kind == OAuthProviderKind::GitHub) {
    id.subject = scalarString(u.at("id"));
    id.login = u.s("login");
    id.displayName = u.s("name");
    if (id.displayName.empty()) id.displayName = id.login;
    id.avatarUrl = u.s("avatar_url");
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    id.emailVerified = false;
    if (!p.emailsUrl.empty()) {
      HttpClientResponse er;
      std::string emailErr;
      if (oauthHttpRequest(p.emailsUrl, "GET", headers, "", &er, &emailErr,
                           cfg_.httpTimeoutMs) &&
          er.status >= 200 && er.status < 300) {
        JVal list;
        if (parseOAuthBody(er.body, &list, &emailErr) && list.type == JVal::ARR) {
          const JVal* chosen = nullptr;
          const JVal* firstVerified = nullptr;
          for (const auto& e : list.arr) {
            if (e.type != JVal::OBJ) continue;
            if (!firstVerified && e.boolean("verified")) firstVerified = &e;
            if (e.boolean("primary") && e.boolean("verified")) {
              chosen = &e;
              break;
            }
          }
          if (!chosen) chosen = firstVerified;
          if (chosen) {
            std::string email = PlayerStore::normalizeEmail(chosen->s("email"));
            if (!email.empty()) {
              id.email = email;
              id.emailVerified = chosen->boolean("verified");
            }
          }
        }
      }
    }
  } else if (p.kind == OAuthProviderKind::Facebook) {
    id.subject = scalarString(u.at("id"));
    id.displayName = u.s("name");
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    // Facebook does not assert email_verified; do not auto-link by email.
    id.emailVerified = false;
    const JVal& picture = u.at("picture").at("data");
    id.avatarUrl = picture.s("url");
  } else if (p.kind == OAuthProviderKind::Discord) {
    id.subject = scalarString(u.at("id"));
    id.login = u.s("username");
    id.displayName = u.s("global_name");
    if (id.displayName.empty()) id.displayName = id.login;
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    id.emailVerified = u.boolean("verified");
    std::string avatar = u.s("avatar");
    if (!avatar.empty()) {
      id.avatarUrl = "https://cdn.discordapp.com/avatars/" + id.subject + "/" +
                     avatar + ".png?size=256";
    }
  } else if (p.kind == OAuthProviderKind::GitLab) {
    id.subject = scalarString(u.at("id"));
    id.login = u.s("username");
    id.displayName = u.s("name");
    if (id.displayName.empty()) id.displayName = id.login;
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    id.emailVerified = !u.s("confirmed_at").empty();
    id.avatarUrl = u.s("avatar_url");
  } else if (p.kind == OAuthProviderKind::Microsoft) {
    id.subject = scalarString(u.at("id"));
    id.displayName = u.s("displayName");
    id.email = PlayerStore::normalizeEmail(u.s("mail"));
    if (id.email.empty())
      id.email = PlayerStore::normalizeEmail(u.s("userPrincipalName"));
    id.emailVerified = !id.email.empty();
    id.login = id.email.empty() ? "" : id.email.substr(0, id.email.find('@'));
  } else {
    id.subject = scalarString(u.at("sub"));
    id.email = PlayerStore::normalizeEmail(u.s("email"));
    id.emailVerified = u.boolean("email_verified");
    id.displayName = u.s("name");
    id.login = u.s("preferred_username");
    if (id.displayName.empty()) id.displayName = id.login;
    id.avatarUrl = u.s("picture");
  }

  if (id.subject.empty() || id.subject.size() > 256) {
    if (err) *err = "provider profile has no stable user id";
    return false;
  }
  if (id.displayName.size() > 64) id.displayName.resize(64);
  if (id.login.size() > 64) id.login.resize(64);
  if (id.avatarUrl.size() > 512) id.avatarUrl.resize(512);
  if (!id.email.empty() && !u.boolean("email_verified", true)) {
    // Keep an explicitly unverified flag from providers that report it.
    id.emailVerified = false;
  }
  *out = std::move(id);
  return true;
}

bool OAuthService::complete(const std::string& providerId,
                            const std::string& code, const std::string& state,
                            const std::string& cookieState, OAuthResult* out,
                            int* status, std::string* err) {
  if (status) *status = 400;
  const OAuthProvider* p = cfg_.provider(providerId);
  if (!p || !p->configured()) {
    if (status) *status = 404;
    if (err) *err = "unknown or unconfigured sign-in provider";
    return false;
  }
  if (state.empty() || cookieState.empty() ||
      !constantTimeEquals(state, cookieState)) {
    if (err) *err = "invalid or missing OAuth state";
    return false;
  }

  StateRecord st;
  bool found = false;
  if (!loadState(state, &st, &found, err)) {
    if (status) *status = 500;
    return false;
  }
  if (!found) {
    if (err) *err = "sign-in session expired; please try again";
    return false;
  }
  // Single use: consume the state before doing any network work so a replayed
  // callback cannot be exchanged twice.
  std::string ignored;
  cache_->del(stateKey(state), &ignored);

  if (st.provider != providerId) {
    if (err) *err = "OAuth state does not match the provider";
    return false;
  }
  if (code.empty()) {
    if (err) *err = "provider did not return an authorization code";
    return false;
  }

  std::string accessToken;
  if (!exchangeCode(*p, code, st.verifier, &accessToken, err)) {
    if (status) *status = 502;
    return false;
  }

  OAuthIdentity identity;
  if (!fetchIdentity(*p, accessToken, &identity, err)) {
    if (status) *status = 502;
    return false;
  }

  PlayerRecord rec;
  bool playerFound = false;
  bool created = false;
  if (!players_->getByOAuth(identity.provider, identity.subject, &rec,
                            &playerFound, err)) {
    if (status) *status = 500;
    return false;
  }

  if (!playerFound && identity.emailVerified && !identity.email.empty()) {
    if (!players_->getByEmail(identity.email, &rec, &playerFound, err)) {
      if (status) *status = 500;
      return false;
    }
  }

  if (playerFound) {
    int linkStatus = 0;
    if (!players_->linkOAuth(&rec, identity, &linkStatus, err)) {
      if (status) *status = linkStatus ? linkStatus : 500;
      return false;
    }
  } else {
    int createStatus = 0;
    if (!players_->createOAuth(identity, &rec, &createStatus, err)) {
      if (status) *status = createStatus ? createStatus : 500;
      return false;
    }
    created = true;
  }

  out->player = std::move(rec);
  out->provider = identity.provider;
  out->providerName = p->name;
  out->returnTo = st.returnTo;
  out->state = st.state;
  out->json = st.json;
  out->created = created;
  if (status) *status = 200;
  return true;
}

}  // namespace dw
