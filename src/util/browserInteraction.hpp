#pragma once

#include "windowsLean.hpp"
#include <string>
#include <string_view>

// Opens an http(s) URL in the user's default browser. Any other scheme
// (file://, custom URL handlers, ...) is refused, since the URL comes from
// the server during external authentication.
void openURLInDefaultBrowser(const std::string& url);

// True if the URL starts with "http://" or "https://" (case-insensitive).
inline bool isHttpUrl(std::string_view url) {
  auto startsWithNoCase = [url](std::string_view prefix) {
    if (url.size() < prefix.size()) {
      return false;
    }
    for (std::size_t i = 0; i < prefix.size(); i++) {
      char c = url[i];
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
      if (c != prefix[i]) {
        return false;
      }
    }
    return true;
  };
  return startsWithNoCase("http://") || startsWithNoCase("https://");
}
