#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "apiAuthMethod.hpp"
#include "authProvider/authConfig.hpp"
#include "authProvider/authCredentials.hpp"
#include "environmentConfig.hpp"
#include "tlsOptions.hpp"

// Session headers of the Trino client protocol.
inline constexpr const char* TRINO_CATALOG_HEADER = "X-Trino-Catalog";
inline constexpr const char* TRINO_SCHEMA_HEADER  = "X-Trino-Schema";

class ConnectionConfig {
  private:
    std::string hostname;
    unsigned short port;
    std::string connectionName;
    std::string tokenEndpoint;
    std::string grantType;
    TlsOptions tlsOptions;

    ApiAuthMethod authMethod;
    std::unique_ptr<AuthConfig> authConfigPtr;
    std::vector<std::function<void(ConnectionConfig*)>> onDisconnectCallbacks;
    std::map<std::string, std::string> nonAuthHeaders;

    // CURL is managed within the connection config. This way
    // we can set up all the right headers and SSL options
    // every time anything asks for a CURL handle.
    CURL* curl;
    struct curl_slist* resolveEntries;
    // Header list currently installed as CURLOPT_HTTPHEADER (owned).
    struct curl_slist* requestHeaders = nullptr;
    char curlErrorBuffer[CURL_ERROR_SIZE];

  public:
    ConnectionConfig(std::string hostname,
                     unsigned short port,
                     ApiAuthMethod authMethod,
                     std::string connectionName,
                     std::string oidcDiscoveryUrl,
                     std::string clientId,
                     std::string clientSecret,
                     std::string oidcScope,
                     std::string grantType,
                     const std::string& tokenEndpoint,
                     const AuthCredentials& credentials = {});

    ~ConnectionConfig();
    // Must be called before the first getCurl().
    void setTlsOptions(const TlsOptions& options);
    // Lifetime of tokens whose expiry can't be read (seconds; 0: until the
    // server rejects them).
    void setTokenLifetime(long long seconds);
    // Call after a response with HTTP 401. True if the credentials can be
    // renewed: the next getCurl() logs in again and the request should be
    // retried once.
    bool renewRejectedCredentials();
    std::string const getHostname();
    std::string const getServerName();
    std::string const getStatementUrl();
    unsigned short const getPort();
    ApiAuthMethod const getAuthMethod();
    CURL* getCurl();
    long getLastHTTPStatusCode();
    void disconnect();
    std::string getTrinoServerVersion();
    void registerDisconnectCallback(std::function<void(ConnectionConfig*)> f);
    void unregisterDisconnectCallback(std::function<void(ConnectionConfig*)> f);
    void setRequestHeader(std::string key, std::string value);
    std::string getRequestHeader(std::string key);
    void clearRequestHeader(std::string key);

    // Making these public because they're frequently accessed and
    // manipulated external to the ConnectionConfig object
    std::string responseData;
    std::map<std::string, std::string> responseHeaderData;
};
