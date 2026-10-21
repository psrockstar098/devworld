#pragma once
// oauth.h — OAuth 2.0 / OpenID Connect sign-in for the platform server.
//
// The flow is the standard authorization-code flow:
//
//   browser ──► /auth/oauth/<provider>/start ──► provider consent screen
//                                                     │
//   browser ◄── /auth/oauth/<provider>/callback ◄──────┘
//                     │
//                     ├─ exchange code for a token
//                     ├─ fetch the provider profile
//                     ├─ find / create / link the player
//                     └─ issue the normal opaque session token
//
// State is single-use and bound to an HttpOnly cookie; PKCE is used wherever
// the provider supports it. Provider secrets never leave the server and are
// only sent in a request body. HTTPS calls use the system `curl` binary, which
// keeps the server free of a linked TLS library; plain http:// is used for
// local tests and self-hosted mock providers.
#include <map>
#include <string>
#include <vector>

#include "http.h"
#include "json.h"
#include "kv.h"
#include "players.h"
#include "sessions.h"

namespace dw {

enum class OAuthProviderKind {
  Google,
  GitHub,
  Facebook,
  Discord,
  GitLab,
  Microsoft,
  OIDC,
};

struct OAuthProvider {
  std::string id;
  std::string name;
  std::string brand = "#64748b";
  OAuthProviderKind kind = OAuthProviderKind::OIDC;
  std::string clientId;
  std::string clientSecret;
  std::string authorizeUrl;
  std::string tokenUrl;
  std::string userInfoUrl;
  std::string emailsUrl;  // GitHub only
  std::string scope;
  std::string extraAuthorizeParams;
  bool pkce = true;

  bool configured() const {
    return !clientId.empty() && !clientSecret.empty() &&
           !authorizeUrl.empty() && !tokenUrl.empty() &&
           !userInfoUrl.empty();
  }
  std::string redirectUri(const std::string& baseUrl) const;
};

struct OAuthConfig {
  std::string baseUrl = "http://127.0.0.1:8091";
  std::string successRedirect;
  std::string allowedOrigins =
      "http://localhost:8000,http://127.0.0.1:8000";
  int stateTtlSec = 600;
  int httpTimeoutMs = 15000;
  std::vector<OAuthProvider> providers;

  static OAuthConfig fromEnv();
  const OAuthProvider* provider(const std::string& id) const;
  bool returnToAllowed(const std::string& url, std::string* origin) const;
};

struct OAuthResult {
  PlayerRecord player;
  std::string provider;
  std::string providerName;
  std::string returnTo;
  std::string state;
  bool json = false;
  bool created = false;
};

// HTTPS (via curl) or HTTP (for tests/self-hosted providers).
bool oauthHttpRequest(const std::string& url, const std::string& method,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body, HttpClientResponse* out,
                      std::string* err, int timeoutMs = 15000);

// Parses a JSON body, falling back to application/x-www-form-urlencoded.
bool parseOAuthBody(const std::string& body, JVal* out, std::string* err);

class OAuthService {
 public:
  OAuthService(OAuthConfig cfg, KvStore* cache, PlayerStore* players,
               SessionStore* sessions)
      : cfg_(std::move(cfg)), cache_(cache), players_(players),
        sessions_(sessions) {}

  bool configured() const {
    for (const auto& p : cfg_.providers)
      if (p.configured()) return true;
    return false;
  }
  const OAuthConfig& config() const { return cfg_; }
  const std::vector<OAuthProvider>& providers() const { return cfg_.providers; }
  const OAuthProvider* provider(const std::string& id) const {
    return cfg_.provider(id);
  }

  // Builds the provider authorization URL and stores the one-time state.
  bool start(const std::string& providerId, const std::string& returnTo,
             bool json, std::string* authorizeUrl, std::string* state,
             int* status, std::string* err);

  // Validates state + cookie, exchanges the code, and resolves the player.
  bool complete(const std::string& providerId, const std::string& code,
                const std::string& state, const std::string& cookieState,
                OAuthResult* out, int* status, std::string* err);

  static std::string stateKey(const std::string& state) {
    return "oauth:state:" + state;
  }

 private:
  struct StateRecord {
    std::string state;
    std::string provider;
    std::string verifier;
    std::string returnTo;
    bool json = false;
  };

  bool loadState(const std::string& state, StateRecord* out, bool* found,
                 std::string* err);
  bool exchangeCode(const OAuthProvider& provider, const std::string& code,
                    const std::string& verifier, std::string* accessToken,
                    std::string* err);
  bool fetchIdentity(const OAuthProvider& provider, const std::string& token,
                     OAuthIdentity* out, std::string* err);

  OAuthConfig cfg_;
  KvStore* cache_;
  PlayerStore* players_;
  SessionStore* sessions_;
};

}  // namespace dw
