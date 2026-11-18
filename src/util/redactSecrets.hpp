#pragma once

#include <map>
#include <string>

// Copy of connection settings that is safe to log: values of keys that look
// like credentials (secret, password, pwd, token, ...) are replaced by ***.
std::map<std::string, std::string>
redactSecretsForLog(const std::map<std::string, std::string>& settings);

bool isSecretSettingName(const std::string& key);
