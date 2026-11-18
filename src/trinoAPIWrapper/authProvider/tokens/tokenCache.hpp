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

  public:
    TokenCacheEntry(std::string accessToken,
                    std::string refreshToken,
                    std::string tokenId);
    bool isExpired();
    void setAccessToken(std::string accessToken);
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

// False when TRINO_ODBC_TOKEN_CACHE=none: tokens are then kept in memory
// only, without touching the cache file or the OS secret store.
bool persistentTokenCacheEnabled();
