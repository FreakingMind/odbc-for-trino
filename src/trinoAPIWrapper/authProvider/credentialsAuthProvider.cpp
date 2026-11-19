#include "credentialsAuthProvider.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../util/base64.hpp"
#include "../apiAuthMethod.hpp"
#include "../trinoExceptions.hpp"
#include "authConfig.hpp"
#include "authCredentials.hpp"

namespace {

// Credentials that never change during a connection: nothing to refresh.
class StaticCredentialsAuthConfig : public AuthConfig {
  public:
    StaticCredentialsAuthConfig(const std::string& hostname,
                                unsigned short port,
                                const std::string& connectionName,
                                const std::string& user,
                                const std::string& authorization)
        : AuthConfig(hostname, port, connectionName) {
      if (!user.empty()) {
        this->headers.insert_or_assign("X-Trino-User", user);
      }
      this->headers.insert_or_assign("Authorization", authorization);
    }

    bool const isExpired() override {
      return false;
    }

    void refresh(CURL* /*curl*/,
                 std::string* /*responseData*/,
                 std::map<std::string, std::string>* /*headerData*/) override {}
};

bool isHttps(const std::string& hostname) {
  std::string scheme = hostname.substr(0, 8);
  std::ranges::transform(scheme, scheme.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return scheme == "https://";
}

// HTTP header values can't contain line breaks: a value with one would
// inject extra headers.
bool hasLineBreak(const std::string& value) {
  return value.find_first_of("\r\n") != std::string::npos;
}

} // namespace

std::string basicAuthorizationValue(const std::string& user,
                                    const std::string& password) {
  const std::string pair = user + ":" + password;
  return "Basic " +
         base64Encode(std::vector<uint8_t>(pair.begin(), pair.end()));
}

std::string effectiveAccessToken(const AuthCredentials& credentials) {
  // A password is normally what the application's login prompt (PWD) just
  // supplied: it wins over a token stored in the data source, which may have
  // expired. Pasted tokens often carry stray spaces or line breaks.
  std::string token = credentials.password.empty() ? credentials.accessToken
                                                   : credentials.password;
  const char* whitespace = " \t\r\n";
  const size_t first     = token.find_first_not_of(whitespace);
  if (first == std::string::npos) {
    return "";
  }
  const size_t last = token.find_last_not_of(whitespace);
  return token.substr(first, last - first + 1);
}

void checkCredentials(ApiAuthMethod authMethod,
                      const std::string& hostname,
                      const AuthCredentials& credentials) {
  if (hasLineBreak(credentials.user)) {
    throw AuthError("The user name contains a line break");
  }
  if (authMethod == AM_PASSWORD) {
    if (credentials.user.empty()) {
      throw AuthError("Password authentication needs a user name (user or "
                      "UID)");
    }
    if (credentials.user.find(':') != std::string::npos) {
      throw AuthError("The user name can't contain ':' with password "
                      "authentication");
    }
    if (credentials.password.empty()) {
      throw AuthError("Password authentication needs a password (password "
                      "or PWD)");
    }
  } else if (authMethod == AM_ACCESS_TOKEN) {
    const std::string token = effectiveAccessToken(credentials);
    if (token.empty()) {
      throw AuthError("Access token authentication needs a token "
                      "(accessToken, or the password)");
    }
    if (hasLineBreak(token)) {
      throw AuthError("The access token contains a line break");
    }
  } else {
    return;
  }
  if (!isHttps(hostname)) {
    throw AuthError("Credentials are only sent over HTTPS: set hostname to "
                    "an https:// URL");
  }
}

std::unique_ptr<AuthConfig>
getPasswordAuthConfigPtr(const std::string& hostname,
                         unsigned short port,
                         const std::string& connectionName,
                         const AuthCredentials& credentials) {
  checkCredentials(AM_PASSWORD, hostname, credentials);
  return std::make_unique<StaticCredentialsAuthConfig>(
      hostname,
      port,
      connectionName,
      credentials.user,
      basicAuthorizationValue(credentials.user, credentials.password));
}

std::unique_ptr<AuthConfig>
getAccessTokenAuthConfigPtr(const std::string& hostname,
                            unsigned short port,
                            const std::string& connectionName,
                            const AuthCredentials& credentials) {
  checkCredentials(AM_ACCESS_TOKEN, hostname, credentials);
  return std::make_unique<StaticCredentialsAuthConfig>(
      hostname,
      port,
      connectionName,
      credentials.user,
      "Bearer " + effectiveAccessToken(credentials));
}
