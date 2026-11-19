#pragma once

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

// The claims of a JWT access token (its payload). Tokens that aren't
// readable JWTs, such as the encrypted tokens Trino issues when refresh
// tokens are enabled, give an empty object: their expiry is unknown.
json parseAccessToken(const std::string& accessToken);
