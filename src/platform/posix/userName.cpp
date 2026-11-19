#include "../userName.hpp"

#include <pwd.h>
#include <unistd.h>

#include <array>
#include <string>

namespace platform {

std::string currentUserName() {
  // The account database, not $USER, which a sandboxed host may not have.
  passwd entry{};
  passwd* result = nullptr;
  std::array<char, 4096> buffer{};
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &result) ==
          0 &&
      result != nullptr && result->pw_name != nullptr) {
    return result->pw_name;
  }
  return "";
}

} // namespace platform
