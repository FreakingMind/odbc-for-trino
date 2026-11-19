#pragma once

#include <memory>
#include <string>

#include "authConfig.hpp"

// No authentication: the server trusts the user name the client sends.
std::unique_ptr<AuthConfig>
getNoAuthConfigPtr(const std::string& hostname,
                   unsigned short port,
                   const std::string& connectionName,
                   const std::string& user);
