#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class TokenCacheEntry {
  private:
    json parsedAccessToken;
    std::string accessToken;
    std::string refreshToken;
    std::string tokenId;
    // When the driver received the access token (Unix seconds, 0 if
    // unknown), and how long a token without an "exp" claim is used before
    // logging in again (0: until the server rejects it).
    long long obtainedAt    = 0;
    long long tokenLifetime = 0;

  public:
    TokenCacheEntry(std::string accessToken,
                    std::string refreshToken,
                    std::string tokenId,
                    long long obtainedAt = 0);
    bool isExpired();
    // Sets a new access token, received now.
    void setAccessToken(std::string accessToken);
    // Forgets the access token, e.g. after the server rejected it.
    void clearAccessToken();
    void setTokenLifetime(long long seconds);
    long long getObtainedAt() const;
    void setRefreshToken(std::string refreshToken);
    json getParsedAccessToken();
    std::string getAccessToken();
    std::string getRefreshToken();
    std::string getTokenId();
};


TokenCacheEntry readTokenCache(const std::string& tokenid);

std::string getTokenIdentity(std::string hostname,
                             unsigned short port,
                             std::string connectionName);

void writeTokenCache(TokenCacheEntry cacheEntry);

// True if a token expiring at `expiresAt` (Unix seconds, from the "exp"
// claim) should be replaced now. The refresh margin is 10 minutes, capped
// at half the token's lifetime when `issuedAt` ("iat") is known (> 0), and
// at 60 seconds when it isn't.
bool tokenNeedsRefresh(long long now, long long expiresAt, long long issuedAt);

// True if `accessToken` must be replaced before the next request. A JWT is
// judged by its "exp" / "iat" claims (see tokenNeedsRefresh). A token
// without a readable expiry (an encrypted or opaque token) is used for
// `tokenLifetime` seconds after `obtainedAt`, or, with a lifetime of 0,
// until the server rejects it (HTTP 401). No token at all needs one.
bool accessTokenNeedsRefresh(long long now,
                             const std::string& accessToken,
                             const json& claims,
                             long long obtainedAt,
                             long long tokenLifetime);

// False when TRINO_ODBC_TOKEN_CACHE=none: tokens are then kept in memory
// only, without touching the cache file or the OS secret store.
bool persistentTokenCacheEnabled();
