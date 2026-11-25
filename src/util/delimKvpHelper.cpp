#include "delimKvpHelper.hpp"

#include "stringTrim.hpp"
#include <algorithm>
#include <sstream>


std::map<std::string, std::string>
parseKVPsFromNullDelimChars(const char* kvpChars) {
  std::map<std::string, std::string> kvps;

  if (!kvpChars) {
    return kvps;
  }

  const char* keyStart = kvpChars;

  while (*keyStart != '\0') {
    const char* separator = keyStart;
    // Attributes are separated by an equals sign.
    while (*separator != '=' and *separator != '\0') {
      separator++;
    }

    if (*separator == '\0') {
      // This shouldn't happen. KVPs need an equals sign.
      break;
    }

    std::string key(keyStart, separator - keyStart);

    const char* valueStart = separator + 1;
    const char* valueEnd   = valueStart;

    // Find the next null character indicating the end
    // of the value
    while (*valueEnd != '\0') {
      valueEnd++;
    }

    std::string value(valueStart, valueEnd - valueStart);

    kvps[key] = value;
    if (*valueEnd == '\0') {
      // The last pair has no trailing delimiter: don't step past the end.
      break;
    }
    keyStart = valueEnd + 1;
  }

  return kvps;
}

std::string toLowercase(std::string s) {
  // This only works for ASCII text
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
  return s;
}

std::string stripQuotes(std::string s, char quoteChar) {
  // Only strip quotes if they're on both ends of the string.
  if (!s.empty() and s.front() == quoteChar and s.back() == quoteChar) {
    s.erase(s.begin());
    s.pop_back();
  }
  return s;
}

namespace {

/*
ODBC connection strings may enclose a value in braces so it can contain the
delimiter: PWD={a;b}. A '}' inside is written as '}}'. On success, sets
*value and returns the position just after the closing brace; returns
nullptr if the brace is never closed.
*/
const char* parseBracedValue(const char* openBrace, std::string* value) {
  value->clear();
  const char* p = openBrace + 1;
  while (*p != '\0') {
    if (*p == '}') {
      if (*(p + 1) == '}') {
        value->push_back('}');
        p += 2;
        continue;
      }
      return p + 1;
    }
    value->push_back(*p);
    p++;
  }
  return nullptr;
}

} // namespace

std::map<std::string, std::string>
parseKVPsFromDelimStr(const std::string& kvpStr,
                      char delim,
                      bool lowercaseKeys,
                      bool trimKeysWhitespace,
                      bool stripDoubleQuotes,
                      bool bracedValues) {
  std::map<std::string, std::string> kvps;

  if (kvpStr.empty()) {
    return kvps;
  }

  const char* keyStart = kvpStr.c_str();

  while (*keyStart != delim and *keyStart != '\0') {
    const char* separator = keyStart;
    // Attributes are separated by an equals sign.
    while (*separator != '=' and *separator != delim and *separator != '\0') {
      separator++;
    }
    if (*separator == delim or *separator == '\0') {
      // This shoudn't happen. KVPs require an equals sign.
      break;
    }

    std::string key(keyStart, separator - keyStart);

    const char* valueStart = separator + 1;
    const char* valueEnd   = valueStart;
    std::string value;
    const char* afterBrace = nullptr;
    if (bracedValues && *valueStart == '{') {
      afterBrace = parseBracedValue(valueStart, &value);
    }
    if (afterBrace != nullptr) {
      // Anything between the closing brace and the delimiter is ignored.
      valueEnd = afterBrace;
      while (*valueEnd != delim and *valueEnd != '\0') {
        valueEnd++;
      }
    } else {
      // Find the delimiter or the end of the string.
      while (*valueEnd != delim and *valueEnd != '\0') {
        valueEnd++;
      }
      value.assign(valueStart, valueEnd - valueStart);
    }

    // We might want to remove any whitespace from the keys
    if (trimKeysWhitespace) {
      trim(key);
    }

    if (stripDoubleQuotes) {
      key   = stripQuotes(key, '"');
      value = stripQuotes(value, '"');
    }

    if (lowercaseKeys) {
      key = toLowercase(key);
    }

    kvps[key] = value;
    if (*valueEnd == '\0') {
      // The last pair has no trailing delimiter: don't step past the end.
      break;
    }
    keyStart = valueEnd + 1;
  }

  return kvps;
}

std::map<std::string, std::string>
parseKVPsFromSemicolonDelimStr(const std::string& kvpStr) {
  char delimiter          = ';';
  bool lowercaseKeys      = true;
  bool trimKeysWhitespace = true;
  bool stripDoubleQuotes  = false;
  const bool bracedValues = true;
  return parseKVPsFromDelimStr(kvpStr,
                               delimiter,
                               lowercaseKeys,
                               trimKeysWhitespace,
                               stripDoubleQuotes,
                               bracedValues);
}

std::map<std::string, std::string>
parseKVPsFromCommaDelimStr(const std::string& kvpStr, bool forceLowercaseKeys) {
  char delimiter          = ',';
  bool lowercaseKeys      = forceLowercaseKeys;
  bool trimKeysWhitespace = true;
  bool stripDoubleQuotes  = true;
  const bool bracedValues = false;
  return parseKVPsFromDelimStr(kvpStr,
                               delimiter,
                               lowercaseKeys,
                               trimKeysWhitespace,
                               stripDoubleQuotes,
                               bracedValues);
}

std::string
encodeMapToCommaDelimString(const std::map<std::string, std::string>& map) {
  std::ostringstream oss;
  for (auto it = map.begin(); it != map.end(); it++) {
    oss << it->first << "=" << it->second;
    if (std::next(it) != map.end()) {
      oss << ", ";
    }
  }
  return oss.str();
}
