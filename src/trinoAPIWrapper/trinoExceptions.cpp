#include "trinoExceptions.hpp"

#include <exception>
#include <stdexcept>
#include <string>

AuthError::AuthError(const std::string& message)
    : std::runtime_error(message) {}

AuthError::AuthError(const char* message) : std::runtime_error(message) {}

ConnectionError::ConnectionError(const std::string& message)
    : std::runtime_error(message) {}

const char* sqlStateFor(const std::exception& error) {
  if (dynamic_cast<const AuthError*>(&error) != nullptr) {
    return "28000";
  }
  if (dynamic_cast<const ConnectionError*>(&error) != nullptr) {
    return "08S01";
  }
  return "HY000";
}
