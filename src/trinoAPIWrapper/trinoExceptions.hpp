#pragma once

#include <exception>
#include <stdexcept>
#include <string>

// Missing, invalid or rejected credentials (SQLSTATE 28000).
class AuthError : public std::runtime_error {
  public:
    explicit AuthError(const std::string& message);
    explicit AuthError(const char* message);
};

// The server could not be reached or the connection broke: DNS, TCP, TLS
// (SQLSTATE 08S01).
class ConnectionError : public std::runtime_error {
  public:
    explicit ConnectionError(const std::string& message);
};

// The SQLSTATE an ODBC entry point reports for an exception: 28000 for
// AuthError, 08S01 for ConnectionError, HY000 for anything else.
const char* sqlStateFor(const std::exception& error);
