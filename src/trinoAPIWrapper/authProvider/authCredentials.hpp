#pragma once

#include <string>

// Credentials given by the data source, the connection string or the
// application (SQLConnect user name and password).
struct AuthCredentials {
    // Trino user (X-Trino-User). For "No Auth" it defaults to the login name
    // of the current user.
    std::string user;
    // Password for "Password" authentication. For "Access Token" it carries
    // the token when given, so applications that only ask for a user name
    // and password (Excel) can pass one; it wins over accessToken.
    std::string password;
    // Bearer token for "Access Token" authentication.
    std::string accessToken;
};
