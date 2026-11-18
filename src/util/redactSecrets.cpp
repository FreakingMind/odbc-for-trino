#include "redactSecrets.hpp"

#include <algorithm>
#include <array>
#include <cctype>

bool isSecretSettingName(const std::string& key) {
  std::string lower = key;
  std::transform(
      lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
  static const std::array<const char*, 5> markers = {
      "secret", "password", "pwd", "token", "passphrase"};
  return std::any_of(markers.begin(), markers.end(), [&](const char* marker) {
    return lower.find(marker) != std::string::npos;
  });
}

std::map<std::string, std::string>
redactSecretsForLog(const std::map<std::string, std::string>& settings) {
  std::map<std::string, std::string> redacted;
  for (const auto& [key, value] : settings) {
    redacted[key] = isSecretSettingName(key) && !value.empty() ? "***" : value;
  }
  return redacted;
}
