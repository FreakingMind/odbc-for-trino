#include "connectionConfig.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#ifdef _WIN32
#include <windns.h>
#include <winsock2.h>
#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#endif

#include "../platform/userName.hpp"
#include "../util/callbackHelper.hpp"
#include "../util/stringTrim.hpp"
#include "../util/writeLog.hpp"
#include "authProvider/clientCredAuthProvider.hpp"
#include "authProvider/credentialsAuthProvider.hpp"
#include "authProvider/deviceFlowAuthProvider.hpp"
#include "authProvider/externalAuthProvider.hpp"
#include "authProvider/noAuthProvider.hpp"


using json = nlohmann::json;

static size_t
curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
  size_t totalSize = size * nmemb;
  s->append(static_cast<char*>(contents), totalSize);
  return totalSize;
}

static size_t
curlHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
  std::map<std::string, std::string>* responseHeaderData =
      (std::map<std::string, std::string>*)userdata;
  std::string headerData = std::string(buffer, nitems);
  if (headerData.starts_with("HTTP/")) {
    // The HTTP/<version> header is not a key value pair, so
    // it gets some special logic.
    responseHeaderData->insert({"http", headerData});
  } else {
    auto firstColon = headerData.find(':');
    if (firstColon != std::string::npos) {
      std::string key   = headerData.substr(0, firstColon);
      std::string value = headerData.substr(firstColon + 1);
      // Normalize header name to lowercase so lookups work regardless of
      // whether the server uses HTTP/1.1 title-case or HTTP/2 lowercase.
      std::transform(key.begin(), key.end(), key.begin(), ::tolower);
      // The values usually end in \r\n at a minimum, so we need
      // to trim that off.
      trim(value);
      auto [it, inserted] = responseHeaderData->insert({key, value});
      if (!inserted) {
        it->second += "\n" + value;
      }
    }
  }
  // Return the number of bytes consumed to signal success.
  return nitems * size;
}

static std::string getServerNameFromHostname(const std::string& hostname) {
  std::string serverName = hostname;

  size_t schemePos = serverName.find("//");
  if (schemePos != std::string::npos) {
    serverName = serverName.substr(schemePos + 2);
  }

  size_t slashPos = serverName.find('/');
  if (slashPos != std::string::npos) {
    serverName = serverName.substr(0, slashPos);
  }

  size_t colonPos = serverName.find(':');
  if (colonPos != std::string::npos) {
    serverName = serverName.substr(0, colonPos);
  }

  return serverName;
}

#ifdef _WIN32
static std::string resolveHostWithWindowsDns(const std::string& serverName) {
  PDNS_RECORDA records = nullptr;
  DNS_STATUS status    = DnsQuery_A(serverName.c_str(),
                                    DNS_TYPE_A,
                                    DNS_QUERY_STANDARD,
                                    nullptr,
                                    &records,
                                    nullptr);

  if (status != ERROR_SUCCESS || records == nullptr) {
    WriteLog(LL_TRACE,
             "  Windows DNS override could not resolve host: " + serverName +
                 "; DNS status was: " + std::to_string(status));
    if (records != nullptr) {
      DnsRecordListFree(records, DnsFreeRecordList);
    }
    return "";
  }

  std::string resolvedIp;
  for (PDNS_RECORDA record = records; record != nullptr;
       record              = record->pNext) {
    if (record->wType == DNS_TYPE_A) {
      IN_ADDR addr;
      addr.S_un.S_addr = record->Data.A.IpAddress;
      char* ip         = inet_ntoa(addr);
      if (ip != nullptr) {
        resolvedIp = ip;
        break;
      }
    }
  }

  DnsRecordListFree(records, DnsFreeRecordList);
  return resolvedIp;
}
#endif


ConnectionConfig::ConnectionConfig(std::string hostname,
                                   unsigned short port,
                                   ApiAuthMethod authMethod,
                                   std::string connectionName,
                                   std::string oidcDiscoveryUrl,
                                   std::string clientId,
                                   std::string clientSecret,
                                   std::string oidcScope,
                                   std::string grantType,
                                   std::string tokenEndpoint,
                                   const AuthCredentials& credentials) {

  this->hostname       = hostname;
  this->port           = port;
  this->connectionName = connectionName;
  this->authMethod     = authMethod;
  this->tokenEndpoint  = tokenEndpoint;
  this->grantType      = grantType;

  this->curl               = nullptr;
  this->resolveEntries     = nullptr;
  this->curlErrorBuffer[0] = '\0';

  if (hostname.empty()) {
    throw std::invalid_argument("hostname");
  }

  checkCredentials(authMethod, hostname, credentials);

  switch (authMethod) {
    case AM_NO_AUTH: {
      // Like the Trino CLI, default to the login name of the current user.
      std::string user = credentials.user;
      if (user.empty()) {
        user = platform::currentUserName();
      }
      if (user.empty()) {
        user = "trino-odbc";
      }
      this->authConfigPtr =
          getNoAuthConfigPtr(hostname, port, connectionName, user);
      break;
    }
    case AM_PASSWORD: {
      this->authConfigPtr =
          getPasswordAuthConfigPtr(hostname, port, connectionName, credentials);
      break;
    }
    case AM_ACCESS_TOKEN: {
      this->authConfigPtr = getAccessTokenAuthConfigPtr(
          hostname, port, connectionName, credentials);
      break;
    }
    case AM_EXTERNAL_AUTH: {
      this->authConfigPtr =
          getExternalAuthConfigPtr(hostname, port, connectionName);
      break;
    }
    case AM_CLIENT_CRED_AUTH: {
      this->authConfigPtr = getClientCredAuthProvider(hostname,
                                                      port,
                                                      connectionName,
                                                      oidcDiscoveryUrl,
                                                      clientId,
                                                      clientSecret,
                                                      oidcScope,
                                                      grantType,
                                                      tokenEndpoint);
      break;
    }
    case AM_DEVICE_FLOW: {
      this->authConfigPtr = getDeviceFlowAuthProvider(hostname,
                                                      port,
                                                      connectionName,
                                                      oidcDiscoveryUrl,
                                                      clientId,
                                                      clientSecret,
                                                      oidcScope);
      break;
    }

    default: {
      // Every later request would dereference the missing provider.
      throw std::invalid_argument("Unsupported authentication method " +
                                  std::to_string(authMethod));
    }
  }
}

ConnectionConfig::~ConnectionConfig() {
  if (this->curl != nullptr) {
    curl_easy_cleanup(this->curl);
    this->curl = nullptr;
  }
  curl_slist_free_all(this->requestHeaders);
  this->requestHeaders = nullptr;
  if (this->resolveEntries != nullptr) {
    curl_slist_free_all(this->resolveEntries);
    this->resolveEntries = nullptr;
  }
}

std::string const ConnectionConfig::getHostname() {
  return this->hostname;
}

/*
Obtain the server name, which is essentially the "netloc"
component of the URL as far as I can tell from the docuentation.
*/
std::string const ConnectionConfig::getServerName() {
  return getServerNameFromHostname(this->hostname);
}

unsigned short const ConnectionConfig::getPort() {
  return this->port;
}

ApiAuthMethod const ConnectionConfig::getAuthMethod() {
  return this->authMethod;
}

void ConnectionConfig::setTlsOptions(const TlsOptions& options) {
  this->tlsOptions = options;
}

void ConnectionConfig::setTokenLifetime(long long seconds) {
  this->authConfigPtr->setTokenLifetime(seconds);
}

bool ConnectionConfig::renewRejectedCredentials() {
  std::string challenge;
  const auto it = this->responseHeaderData.find("www-authenticate");
  if (it != this->responseHeaderData.end()) {
    challenge = it->second;
  }
  const bool renew = this->authConfigPtr->onCredentialsRejected(challenge);
  if (renew) {
    WriteLog(LL_INFO,
             "  Trino rejected the access token (HTTP 401): logging in again");
  }
  return renew;
}

std::string const ConnectionConfig::getStatementUrl() {
  return (this->hostname) + ":" + std::to_string(port) + "/v1/statement";
}

CURL* ConnectionConfig::getCurl() {
  /*
  Return a curl handle, reset it if needed, and it's ready to go.
  */
  if (this->curl == nullptr) {
    this->curl = curl_easy_init();
    // We always want to use SSL.
    // REVOKE_BEST_EFFORT: Schannel checks certificate revocation via
    // OCSP/CRL. When the OCSP responder (ocsp.globalsign.com) is not
    // reachable from the client network, Schannel fails the handshake with
    // CRYPT_E_REVOCATION_OFFLINE and libcurl returns CURLE_SSL_CONNECT_ERROR
    // (35). Best effort ignores "revocation server offline / no check"
    // errors, but still rejects certificates that are actually revoked.
    curl_easy_setopt(
        this->curl,
        CURLOPT_SSL_OPTIONS,
        (long)(CURLSSLOPT_NATIVE_CA | CURLSSLOPT_REVOKE_BEST_EFFORT));
    // Certificate verification level and optional CA bundle (DSN options
    // sslVerification / sslCaFile).
    applyTlsOptions(this->curl, this->tlsOptions);
    // Keep the detailed libcurl/Schannel error text so it can be logged.
    this->curlErrorBuffer[0] = '\0';
    curl_easy_setopt(this->curl, CURLOPT_ERRORBUFFER, this->curlErrorBuffer);
    curl_easy_setopt(this->curl, CURLOPT_PRIVATE, this->curlErrorBuffer);
    // We want to save the response body in a string using a callback.
    curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &(this->responseData));
    // We want to parse response headers.
    curl_easy_setopt(this->curl, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
    curl_easy_setopt(
        this->curl, CURLOPT_HEADERDATA, &(this->responseHeaderData));
    // Set a timeout on all requests
    curl_easy_setopt(this->curl, CURLOPT_TIMEOUT_MS, 60000);
    // Keep internal Trino traffic direct. Some Windows corporate proxy/VPN
    // setups make libcurl DNS resolution fail for internal names.
    curl_easy_setopt(this->curl, CURLOPT_PROXY, "");
    curl_easy_setopt(this->curl, CURLOPT_NOPROXY, "*");
    // Trino endpoint is IPv4 in the target environment; do not try IPv6 first.
    curl_easy_setopt(this->curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    // Match the working curl.exe diagnostic path and avoid HTTP/2 edge cases.
    curl_easy_setopt(this->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#ifdef _WIN32
    std::string serverName = this->getServerName();
    std::string resolvedIp = resolveHostWithWindowsDns(serverName);
    if (!resolvedIp.empty()) {
      std::string resolveEntry =
          serverName + ":" + std::to_string(this->port) + ":" + resolvedIp;
      this->resolveEntries = curl_slist_append(nullptr, resolveEntry.c_str());
      curl_easy_setopt(this->curl, CURLOPT_RESOLVE, this->resolveEntries);
      WriteLog(LL_TRACE,
               "  Windows DNS override for libcurl is: " + resolveEntry);
    }
#endif
    // A password or token is attached to every request, including the
    // nextUri links the server returns: never let one of those be plain
    // HTTP (e.g. behind a TLS-terminating proxy).
    if (this->authMethod == AM_PASSWORD ||
        this->authMethod == AM_ACCESS_TOKEN) {
      curl_easy_setopt(this->curl, CURLOPT_PROTOCOLS_STR, "https");
      if (this->tlsOptions.verification == SslVerification::None) {
        WriteLog(LL_WARN,
                 "  WARNING: sslVerification=NONE: the server's identity "
                 "isn't checked, so the credentials could reach an "
                 "impostor");
      }
    }
    // Enable gzip and/or deflate on responses
    curl_easy_setopt(this->curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
  }

  bool refreshedThisCall = false;

curlSetup:
  // Clear the previous response data, we do not want to append to it.
  this->responseData.clear();
  // Clear the previous response headers as well
  this->responseHeaderData.clear();

  // We could do a full reset here, but that seems to slow the driver
  // down considerably. Better to just reset a few things and
  // otherwise reuse the curl handle.
  // curl_easy_reset(this->curl);

  // Let's say the standard state of curl is that the
  // handle is configured to run GET requests, no matter
  // how it was used before.
  curl_easy_setopt(this->curl, CURLOPT_HTTPGET, true);
  // Cancelling a query sends a DELETE via CURLOPT_CUSTOMREQUEST; without a
  // reset every later request on the connection would be a DELETE too.
  curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, nullptr);

  // Set up any required headers if needed. We may need to handle
  // both auth headers and non-auth headers.
  struct curl_slist* headers = nullptr;
  for (const auto& pair : this->nonAuthHeaders) {
    std::string nextHeader = pair.first + ": " + pair.second;
    headers                = curl_slist_append(headers, nextHeader.c_str());
  }
  if (this->authConfigPtr->headers.size() > 0) {
    for (const auto& pair : this->authConfigPtr->headers) {
      std::string nextHeader = pair.first + ": " + pair.second;
      headers                = curl_slist_append(headers, nextHeader.c_str());
    }
  }
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  // curl keeps a pointer to the list: free the previous one only now that
  // it's no longer installed. (Previously every request leaked its list.)
  curl_slist_free_all(this->requestHeaders);
  this->requestHeaders = headers;

  // Now that we have a fully configured CURL handle, check if we need to do
  // any required auth steps. We may need to use the configured handle to
  // perform the authentication.
  //
  // Authenticate at most once per call: if the identity provider hands out
  // a token that already looks expired, retrying would loop forever.
  if (this->authConfigPtr->isExpired()) {
    if (refreshedThisCall) {
      WriteLog(LL_WARN,
               "  WARNING: The new access token is already (nearly) expired; "
               "using it anyway");
    } else {
      WriteLog(LL_TRACE,
               "  Detected expired authentication. Reauthenticating...");
      this->authConfigPtr->refresh(
          this->curl, &(this->responseData), &(this->responseHeaderData));
      refreshedThisCall = true;
      goto curlSetup;
    }
  }

  return this->curl;
}

long ConnectionConfig::getLastHTTPStatusCode() {
  long httpStatusCode = -1;
  if (this->curl) {
    curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &httpStatusCode);
  }
  return httpStatusCode;
}

void ConnectionConfig::disconnect() {
  for (std::function f : this->onDisconnectCallbacks) {
    f(this);
  }
  if (this->curl != nullptr) {
    curl_easy_cleanup(this->curl);
    this->curl = nullptr;
  }
  curl_slist_free_all(this->requestHeaders);
  this->requestHeaders = nullptr;
  if (this->resolveEntries != nullptr) {
    curl_slist_free_all(this->resolveEntries);
    this->resolveEntries = nullptr;
  }
}

std::string ConnectionConfig::getTrinoServerVersion() {
  CURL* curl = this->getCurl();

  std::string url =
      this->hostname + ":" + std::to_string(this->port) + "/v1/info";

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    WriteLog(LL_ERROR,
             "Failed to read trino server version: " +
                 std::string(curl_easy_strerror(res)) + " | " +
                 std::string(this->curlErrorBuffer));
    return "";
  }

  json jsonResponse = nlohmann::json::parse(this->responseData);
  return jsonResponse["nodeVersion"]["version"].get<std::string>();
}

void ConnectionConfig::registerDisconnectCallback(
    std::function<void(ConnectionConfig*)> f) {
  this->onDisconnectCallbacks.push_back(f);
}

void ConnectionConfig::unregisterDisconnectCallback(
    std::function<void(ConnectionConfig*)> f) {
  removeCallbackFromVector(this->onDisconnectCallbacks, f);
}

void ConnectionConfig::setRequestHeader(std::string key, std::string value) {
  // Specifically must use [] and not map.insert, since the latter will not
  // overwrite existing keys.
  this->nonAuthHeaders[key] = value;
}

std::string ConnectionConfig::getRequestHeader(std::string key) {
  if (this->nonAuthHeaders.count(key) > 0) {
    return this->nonAuthHeaders[key];
  } else {
    return "";
  }
}

void ConnectionConfig::clearRequestHeader(std::string key) {
  this->nonAuthHeaders.erase(key);
}
