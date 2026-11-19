#pragma once

#include <memory>
#include <string>

#include "../apiAuthMethod.hpp"
#include "authConfig.hpp"
#include "authCredentials.hpp"

// "Basic <base64(user:password)>" (RFC 7617).
std::string basicAuthorizationValue(const std::string& user,
                                    const std::string& password);

// Checks the credentials of the "Password" and "Access Token" methods before
// anything is sent: both need the values they use and an https:// server,
// since credentials must never travel in clear text. Throws AuthError.
void checkCredentials(ApiAuthMethod authMethod,
                      const std::string& hostname,
                      const AuthCredentials& credentials);

// The token used by "Access Token": the password if one is given (the
// application's login prompt), otherwise accessToken; trimmed.
std::string effectiveAccessToken(const AuthCredentials& credentials);

std::unique_ptr<AuthConfig>
getPasswordAuthConfigPtr(const std::string& hostname,
                         unsigned short port,
                         const std::string& connectionName,
                         const AuthCredentials& credentials);

std::unique_ptr<AuthConfig>
getAccessTokenAuthConfigPtr(const std::string& hostname,
                            unsigned short port,
                            const std::string& connectionName,
                            const AuthCredentials& credentials);
