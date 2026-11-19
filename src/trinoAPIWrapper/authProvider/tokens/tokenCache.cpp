#include "tokenCache.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "../../../platform/platformPaths.hpp"
#include "../../../util/b64decoder.hpp"
#include "../../../util/cryptUtils.hpp"
#include "../../../util/stringSplitAndTrim.hpp"
#include "../../../util/timeUtils.hpp"
#include "../../../util/writeLog.hpp"
#include "tokenParser.hpp"


using json = nlohmann::json;


int TOKEN_CACHE_JSON_INDENT               = 2;
long long EXPIRY_GRACE_PERIOD_S           = 60 * 10;
long long UNKNOWN_LIFETIME_GRACE_PERIOD_S = 60;

bool tokenNeedsRefresh(long long now, long long expiresAt, long long issuedAt) {
  if (expiresAt <= 0) {
    // No usable "exp" claim (or no token at all).
    return true;
  }
  // Refresh ahead of expiry so long-running queries don't outlive the
  // token, but never by more than half the token's lifetime: identity
  // providers commonly issue 5 minute tokens (Keycloak's default), which a
  // fixed 10 minute margin would treat as expired the moment they arrive.
  long long grace = EXPIRY_GRACE_PERIOD_S;
  if (issuedAt > 0 && expiresAt > issuedAt) {
    grace = std::min(grace, (expiresAt - issuedAt) / 2);
  } else {
    // Unknown lifetime: a large margin could exceed it and make every fresh
    // token look expired (one login per request), so keep it small.
    grace = std::min(grace, UNKNOWN_LIFETIME_GRACE_PERIOD_S);
  }
  return expiresAt - now < grace;
}


bool accessTokenNeedsRefresh(long long now,
                             const std::string& accessToken,
                             const json& claims,
                             long long obtainedAt,
                             long long tokenLifetime) {
  if (accessToken.empty()) {
    return true;
  }
  const auto expiresAt = claims.value<long long>("exp", 0LL);
  if (expiresAt > 0) {
    return tokenNeedsRefresh(
        now, expiresAt, claims.value<long long>("iat", 0LL));
  }
  if (tokenLifetime <= 0) {
    // Unknown expiry: keep using it until the server rejects it.
    return false;
  }
  if (obtainedAt <= 0) {
    // Its age is unknown (e.g. cached by an older driver version).
    return true;
  }
  return tokenNeedsRefresh(now, obtainedAt + tokenLifetime, obtainedAt);
}


TokenCacheEntry::TokenCacheEntry(std::string accessToken,
                                 std::string refreshToken,
                                 std::string tokenId,
                                 long long obtainedAt) {
  this->accessToken  = accessToken;
  this->refreshToken = refreshToken;
  this->tokenId      = tokenId;
  this->obtainedAt   = obtainedAt;
  // An object, never null, so the claims can be read with value().
  this->parsedAccessToken = this->accessToken.empty()
                                ? json::object()
                                : parseAccessToken(this->accessToken);
}


bool TokenCacheEntry::isExpired() {
  const long long now = getSecondsSinceEpoch();
  if (getLogLevel() <= LL_TRACE) {
    WriteLog(LL_TRACE,
             "  Token exp: " +
                 std::to_string(
                     this->parsedAccessToken.value<long long>("exp", 0LL)) +
                 ", obtained at: " + std::to_string(this->obtainedAt) +
                 ", now: " + std::to_string(now));
  }
  return accessTokenNeedsRefresh(now,
                                 this->accessToken,
                                 this->parsedAccessToken,
                                 this->obtainedAt,
                                 this->tokenLifetime);
}


void TokenCacheEntry::setAccessToken(std::string accessToken) {
  this->accessToken       = accessToken;
  this->parsedAccessToken = parseAccessToken(this->accessToken);
  this->obtainedAt        = getSecondsSinceEpoch();
}

void TokenCacheEntry::clearAccessToken() {
  this->accessToken       = "";
  this->parsedAccessToken = json::object();
  this->obtainedAt        = 0;
}

void TokenCacheEntry::setTokenLifetime(long long seconds) {
  this->tokenLifetime = seconds;
}

long long TokenCacheEntry::getObtainedAt() const {
  return this->obtainedAt;
}

void TokenCacheEntry::setRefreshToken(std::string refreshToken) {
  this->refreshToken = refreshToken;
}

json TokenCacheEntry::getParsedAccessToken() {
  return this->parsedAccessToken;
}

std::string TokenCacheEntry::getAccessToken() {
  return this->accessToken;
}


std::string TokenCacheEntry::getRefreshToken() {
  return this->refreshToken;
}


std::string TokenCacheEntry::getTokenId() {
  return this->tokenId;
}

bool persistentTokenCacheEnabled() {
  // TRINO_ODBC_TOKEN_CACHE=none keeps tokens in memory only (no cache file,
  // no OS secret store), e.g. for tests and CI. A DSN option and more
  // storage modes are planned.
  const char* mode = std::getenv("TRINO_ODBC_TOKEN_CACHE");
  return !(mode && std::string(mode) == "none");
}

TokenCacheEntry readTokenCache(const std::string& tokenId) {
  if (!persistentTokenCacheEnabled()) {
    WriteLog(LL_DEBUG, "  Persistent token cache disabled");
    return TokenCacheEntry("", "", tokenId);
  }
  std::filesystem::path filePath = platform::tokenCacheFilePath();

  WriteLog(LL_TRACE,
           std::wstring(L"  Token cache file path is: ") + filePath.wstring());

  // Handle the case that the file doesn't exist.
  if (!std::filesystem::exists(filePath)) {
    WriteLog(LL_WARN,
             "  WARNING: Token cache file does not exist. Is this the first "
             "run of the driver?");
    return TokenCacheEntry("", "", tokenId);
  }

  // Handle the case that the file can't be parsed as JSON.
  std::ifstream inputFile(filePath);
  json jsonData;
  try {
    inputFile >> jsonData;
  } catch (const std::exception& e) {
    WriteLog(LL_ERROR,
             "  ERROR: failed to parse token cache file as JSON: " +
                 std::string(e.what()));
    inputFile.close();
    return TokenCacheEntry("", "", tokenId);
  }

  // Close the file, at this point we have parsed it to JSON.
  inputFile.close();

  // Get the tokens, if they are available. Be sure to handle the case that
  // we're trying to read a tokenId that's not present in the file. We want to
  // default to empty strings for the tokens in that case.
  json tokenData = jsonData.value(tokenId, json(json::value_t::object));
  std::string encryptedAccessToken =
      tokenData.value("encryptedAccessToken", "");
  std::string encryptedRefreshToken =
      tokenData.value("encryptedRefreshToken", "");
  const auto obtainedAt = tokenData.value<long long>("obtainedAt", 0LL);

  try {
    std::string accessToken  = userDecryptString(encryptedAccessToken);
    std::string refreshToken = userDecryptString(encryptedRefreshToken);

    // Return a cache entry for these.
    WriteLog(LL_TRACE, "  Token cache read successfully");
    return {accessToken, refreshToken, tokenId, obtainedAt};
  } catch (const std::runtime_error& e) {
    // There was an error getting the encrypted content. Clean it up and remove
    // from TrinoODBCTokenCache.json
    WriteLog(LL_ERROR,
             "  ERROR: Failed to decrypt token from cache: " +
                 std::string(e.what()));
    WriteLog(LL_WARN,
             "  Deleting potentially corrupted token entry from cache.");

    // Remove the corrupted token data
    if (jsonData.contains(tokenId)) {
      jsonData.erase(tokenId);
      std::ofstream outputFile(filePath);
      if (outputFile.is_open()) {
        outputFile << jsonData.dump(TOKEN_CACHE_JSON_INDENT);
        outputFile.close();
      } else {
        WriteLog(LL_ERROR,
                 "  ERROR: Could not open token cache file to delete "
                 "corrupted entry.");
      }
    }
    return TokenCacheEntry("", "", tokenId);
  }
}


void writeTokenCache(TokenCacheEntry cacheEntry) {
  if (!persistentTokenCacheEnabled()) {
    return;
  }
  std::filesystem::path filePath = platform::tokenCacheFilePath();

  // Start by reading the cache file in its current state.
  std::ifstream inputFile(filePath);
  json inputJsonData;
  if (!inputFile.is_open()) {
    WriteLog(LL_WARN,
             "  ERROR: cannot open token cache file for reading. First time "
             "writing to token cache?");
  } else {
    WriteLog(LL_TRACE,
             "  Reading from token cache before overwriting it fresh");
    try {
      inputFile >> inputJsonData;
    } catch (const std::exception& e) {
      WriteLog(LL_WARN,
               "  WARNING: token cache file is not valid JSON, rewriting it: " +
                   std::string(e.what()));
      inputJsonData = json::object();
    }
    inputFile.close();
  }

  // Modify the input with the new token(s) we wish to write.
  std::string tokenId    = cacheEntry.getTokenId();
  inputJsonData[tokenId] = json::object();
  // Caching is an optimization: if the OS secret store is unavailable (e.g.
  // the user denied keychain access or there's no UI to ask), log it and
  // keep the connection working without a persistent cache.
  try {
    inputJsonData[tokenId]["encryptedAccessToken"] =
        userEncryptString(cacheEntry.getAccessToken());
    inputJsonData[tokenId]["encryptedRefreshToken"] =
        userEncryptString(cacheEntry.getRefreshToken());
    inputJsonData[tokenId]["obtainedAt"] = cacheEntry.getObtainedAt();
  } catch (const std::exception& e) {
    WriteLog(LL_WARN,
             "  WARNING: Not caching tokens, encryption failed: " +
                 std::string(e.what()));
    return;
  }

  // The tokens are encrypted, but keep the file private to the user anyway:
  // make sure it exists and is owner-only *before* any token is written.
  {
    std::ofstream create(filePath, std::ios_base::app);
  }
  std::error_code ec;
  std::filesystem::permissions(filePath,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace,
                               ec);

  // Last, write it back with the updated token cache.
  std::ofstream outputFile(filePath, std::ios_base::trunc);
  if (!outputFile.is_open()) {
    WriteLog(LL_ERROR, "  ERROR: cannot open token cache file for writing");
    return;
  }
  outputFile << inputJsonData.dump(TOKEN_CACHE_JSON_INDENT);
  outputFile.close();
}


std::string getTokenIdentity(std::string hostname,
                             unsigned short port,
                             std::string connectionName) {
  return connectionName + "__" + hostname + "__" + std::to_string(port);
}
