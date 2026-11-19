#include "../userName.hpp"

#include "../../util/windowsLean.hpp"
#include <lmcons.h>

#include <string>

namespace platform {

std::string currentUserName() {
  wchar_t name[UNLEN + 1] = {};
  DWORD length            = UNLEN + 1;
  if (!GetUserNameW(name, &length)) {
    return "";
  }
  const int bytes =
      WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return "";
  }
  std::string utf8(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, name, -1, utf8.data(), bytes, nullptr, nullptr);
  utf8.resize(static_cast<size_t>(bytes) - 1);
  return utf8;
}

} // namespace platform
