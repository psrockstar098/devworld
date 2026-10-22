#pragma once
// api.h — the DEVWORLD HTTP/WebSocket API. Handlers are pure with respect to
// the injected stores, so the whole surface is unit-tested against in-memory
// doubles and integration-tested against the real ClickHouse/Redis/Kvrocks.
#include <deque>
#include <mutex>
#include <string>

#include "assess.h"
#include "clickhouse.h"
#include "events.h"
#include "friends.h"
#include "http.h"
#include "kv.h"
#include "market.h"
#include "missions.h"
#include "oauth.h"
#include "players.h"
#include "presence.h"
#include "ratelimit.h"
#include "sessions.h"
#include "ws.h"

namespace dw {

struct ApiConfig {
  std::string adminKey;
  int sessionTtlSec = 604800;
  int balanceCacheTtlSec = 15;
  OAuthConfig oauth;
  // Auth rate limiting: fixed windows shared by /auth/register and /auth/login.
  int authRateLimitWindowSec = 60;
  int authRateLimitMaxPerIp = 30;
  int authRateLimitMaxPerAccount = 10;
};

class Api {
 public:
  Api(History* history, KvStore* kv, KvStore* cache, PlayerStore* players,
      SessionStore* sessions, MissionCatalog* missions, AssessClient* assess,
      ApiConfig cfg, const MarketCatalog* markets = nullptr);
  ~Api();

  HttpResponse handle(const HttpRequest& req);
  void handleUpgrade(Socket& sock, const HttpRequest& req);
  WsHub& hub() { return hub_; }

 private:
  HttpResponse dispatch(const HttpRequest& req);
  struct Auth {
    bool ok = false;
    std::string playerId;
    HttpResponse response;
  };

  Auth requirePlayer(const HttpRequest& req);
  std::string tokenOf(const HttpRequest& req) const;

  HttpResponse handleHealth();
  HttpResponse handleRegister(const HttpRequest& req);
  HttpResponse handleLogin(const HttpRequest& req);
  // False when the auth attempt is rate limited; limited holds the 429.
  bool authAllowed(const HttpRequest& req, const std::string& account,
                   HttpResponse* limited);
  HttpResponse handleLogout(const HttpRequest& req);
  HttpResponse handleOAuthProviders();
  HttpResponse handleOAuthStart(const HttpRequest& req, const std::string& provider);
  HttpResponse handleOAuthCallback(const HttpRequest& req,
                                   const std::string& provider);
  HttpResponse oauthFailure(const HttpRequest& req, int status,
                            const std::string& message, bool json);
  HttpResponse oauthSuccessPage(const OAuthResult& result,
                                const IssuedSession& session);
  std::string cookieOf(const HttpRequest& req, const std::string& name) const;
  HttpResponse handleMe(const HttpRequest& req);
  HttpResponse handleGetPlayer(const std::string& id);
  HttpResponse handleMissions();
  HttpResponse handleMissionStats();
  HttpResponse handleSubmit(const HttpRequest& req, const std::string& id);
  HttpResponse handleBalance(const HttpRequest& req);
  HttpResponse handleAdminLedger(const HttpRequest& req);
  HttpResponse handleUpdateAppearance(const HttpRequest& req);
  HttpResponse handleWorldPlayers(const HttpRequest& req);
  HttpResponse handleFriends(const HttpRequest& req);
  HttpResponse handleFriendRequest(const HttpRequest& req);
  HttpResponse handleFriendAccept(const HttpRequest& req);
  HttpResponse handleFriendDecline(const HttpRequest& req);
  HttpResponse handleFriendRemove(const std::string& id, const HttpRequest& req);
  HttpResponse handleRecordEvent(const HttpRequest& req);
  HttpResponse handleListEvents(const HttpRequest& req);
  HttpResponse handleMarketList(const HttpRequest& req);
  HttpResponse handleMarketBuy(const HttpRequest& req);
  HttpResponse handleMarketInventory(const HttpRequest& req);
  HttpResponse handleMarketRestock(const HttpRequest& req);

  void ensureHome(PlayerRecord* rec);
  JVal friendView(const std::string& playerId);
  JVal chatHistoryJson();
  void addChat(const std::string& messageJson);

  bool balanceOf(const std::string& playerId, int64_t* out, std::string* err);
  bool totalXpOf(const std::string& playerId, int64_t* out, std::string* err);

  History* history_;
  KvStore* kv_;
  KvStore* cache_;
  PlayerStore* players_;
  SessionStore* sessions_;
  MissionCatalog* missions_;
  AssessClient* assess_;
  ApiConfig cfg_;
  RateLimiter ipLimiter_;
  RateLimiter accountLimiter_;
  OAuthService oauth_;
  WsHub hub_;
  FriendStore friends_;
  PresenceStore presence_;
  EventStore events_;
  MarketStore markets_;
  const MarketCatalog* marketCatalog_;
  // Serializes market buys and restocks: demand, inventory and the ledger
  // move together per process, so two concurrent buys cannot both pay the
  // pre-purchase price for the same demand level.
  std::mutex marketMu_;
  // Recent world chat, kept in memory so a fresh connection sees context.
  // Every message is also written to the history store.
  std::mutex chatMu_;
  std::deque<std::string> chatLog_;
};

}  // namespace dw
