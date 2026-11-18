#include "../platformPaths.hpp"

#include <cstdlib>
#include <pwd.h>
#include <system_error>
#include <unistd.h>

/*
macOS locations.

Excel for Mac is sandboxed: inside the sandbox $HOME points to the app
container (~/Library/Containers/com.microsoft.Excel/Data), which is exactly
where the driver is allowed to write. Outside the sandbox (CLI tools, tests)
$HOME is the real home directory. Either way, deriving paths from $HOME gives
a writable location.
*/

namespace {

const char* APP_DIR_NAME = "TrinoODBC";

std::filesystem::path homeDirectory() {
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home);
  }
  if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) {
    return std::filesystem::path(pw->pw_dir);
  }
  return {};
}

// Creates the directory (private to the user) if it doesn't exist yet.
std::filesystem::path ensureDirectory(const std::filesystem::path& dir) {
  std::error_code ec;
  if (std::filesystem::create_directories(dir, ec)) {
    std::filesystem::permissions(dir,
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace,
                                 ec);
  }
  return dir;
}

} // namespace

namespace platform {

std::filesystem::path tokenCacheFilePath() {
  std::filesystem::path home = homeDirectory();
  if (home.empty()) {
    return {};
  }
  return ensureDirectory(home / "Library" / "Application Support" /
                         APP_DIR_NAME) /
         "TrinoODBCTokenCache.json";
}

std::filesystem::path defaultLogFilePath() {
  std::filesystem::path home = homeDirectory();
  if (home.empty()) {
    return std::filesystem::path("/tmp") / "TrinoODBC.log";
  }
  return ensureDirectory(home / "Library" / "Logs" / APP_DIR_NAME) /
         "TrinoODBC.log";
}

} // namespace platform
