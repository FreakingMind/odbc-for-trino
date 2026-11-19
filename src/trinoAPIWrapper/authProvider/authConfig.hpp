#pragma once

#include <curl/curl.h>
#include <map>
#include <string>

class AuthConfig {
  public:
    // Any headers that must be included on all requests go here.
    std::map<std::string, std::string> headers = {
        {"X-Trino-Source", "TrinoODBCDriver"},
    };
    std::string hostname;
    unsigned short port;
    std::string connectionName;

    AuthConfig(std::string hostname,
               unsigned short port,
               std::string connectionName) {
      this->hostname       = hostname;
      this->port           = port;
      this->connectionName = connectionName;
    }

    // The '=0' on the end makes these "pure virtual" methods,
    // transforming this into an abstract bass class.
    virtual bool const isExpired()                                       = 0;
    virtual void refresh(CURL* curl,
                         std::string* responseData,
                         std::map<std::string, std::string>* headerData) = 0;

    // The server rejected the credentials (HTTP 401). Providers that obtain
    // tokens drop the current one so the next request logs in again, and
    // return true: the request is then retried once. `challenge` is the
    // response's WWW-Authenticate header, which may say how to get a new
    // token. Fixed credentials can't be renewed: the default returns false.
    virtual bool onCredentialsRejected(const std::string& /*challenge*/) {
      return false;
    }

    // How long a token whose expiry can't be read is used (seconds; 0:
    // until the server rejects it). Only token providers use it.
    virtual void setTokenLifetime(long long /*seconds*/) {}

    // Virtual destructors are considered "best practice" for virtual classes.
    virtual ~AuthConfig() = default;
};
