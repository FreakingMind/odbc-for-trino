#include "../../util/browserInteraction.hpp"
#include <shellapi.h>

void openURLInDefaultBrowser(const std::string& url) {
  if (!isHttpUrl(url)) {
    MessageBox(nullptr,
               "Refusing to open a non-http(s) authentication URL",
               "Authentication Error",
               MB_ICONERROR);
    return;
  }

  HINSTANCE result =
      ShellExecute(nullptr, nullptr, url.c_str(), nullptr, nullptr, SW_SHOW);

  if (reinterpret_cast<intptr_t>(result) <= 32) {
    MessageBox(nullptr,
               "Failed to open default browser",
               "Authentication Error",
               MB_ICONERROR);
  }
}
