#pragma once

#include <map>
#include <optional>
#include <string>

#include "curl/curl.h"

#include "authConfig.hpp"
#include "tokens/tokenCache.hpp"


class TokenCacheAuthProviderBase : public AuthConfig {

  public:
    TokenCacheAuthProviderBase(std::string hostname,
                               unsigned short port,
                               std::string connectionName);
    bool const isExpired() override;
    void applyToken();
    void
    refresh(CURL* curl,
            std::string* responseData,
            std::map<std::string, std::string>* responseHeaderData) override;
    bool onCredentialsRejected(const std::string& challenge) override;
    void setTokenLifetime(long long seconds) override;
    virtual ~TokenCacheAuthProviderBase() = default;

  protected:
    std::string tokenId;
    std::optional<TokenCacheEntry> tokenCache;
    // WWW-Authenticate of the response that rejected the last token: tells
    // External Auth where to get a new one. Consumed by the next login.
    std::string rejectionChallenge;
    // This is pure virtual, it requires an implementation in the subclass.
    virtual std::string obtainAccessToken(
        CURL* curl,
        std::string* responseData,
        std::map<std::string, std::string>* responseHeaderData) = 0;
    void applyTokenIfNotExpired();
    void cacheToken(std::string accessToken);
    void setAccessTokenHeader(std::string accessToken);
};
