#include "noAuthProvider.hpp"

#include <curl/curl.h>

#include <map>
#include <memory>
#include <string>

#include "authConfig.hpp"

namespace {

// The default AuthConfig is sufficient for a Trino coordinator with
// authentication disabled; it only needs the user name.
class NoAuthAuthConfig : public AuthConfig {
  public:
    NoAuthAuthConfig(const std::string& hostname,
                     unsigned short port,
                     const std::string& connectionName,
                     const std::string& user)
        : AuthConfig(hostname, port, connectionName) {
      this->headers.insert_or_assign("X-Trino-User", user);
    }

    bool const isExpired() override {
      return false;
    }
    void refresh(CURL* /*curl*/,
                 std::string* /*responseData*/,
                 std::map<std::string, std::string>* /*headerData*/) override {
      // There's nothing to refresh.
    }
};

} // namespace

std::unique_ptr<AuthConfig>
getNoAuthConfigPtr(const std::string& hostname,
                   unsigned short port,
                   const std::string& connectionName,
                   const std::string& user) {
  return std::make_unique<NoAuthAuthConfig>(
      hostname, port, connectionName, user);
}
