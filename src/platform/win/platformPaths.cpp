#include "../platformPaths.hpp"

#include <system_error>

#include "../../util/windowsLean.hpp"
#include <shlobj.h> // For getting windows folder paths.

namespace platform {

std::filesystem::path tokenCacheFilePath() {
  PWSTR tempPath = nullptr;
  // https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath
  if (SUCCEEDED(
          SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &tempPath))) {
    std::filesystem::path result(tempPath);
    // We are responsible for freeing the memory used to return the tempPath.
    CoTaskMemFree(tempPath);
    return result / L"Temp" / L"TrinoODBCTokenCache.json";
  }
  CoTaskMemFree(tempPath);
  return {};
}

std::filesystem::path defaultLogFilePath() {
  // Kept at the historical location so existing setups find their logs.
  std::filesystem::path dir("C:\\temp");
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir / "odbclog.txt";
}

} // namespace platform
