#pragma once

#include <filesystem>

/*
Per-platform file locations used by the driver. Implementations live in
src/platform/<os>/platformPaths.cpp.
*/
namespace platform {

// Full path of the JSON file that caches OAuth tokens between connections.
// The parent directory is created if needed. Returns an empty path if no
// suitable location can be determined.
std::filesystem::path tokenCacheFilePath();

// Default log file used until a DSN overrides it. The parent directory is
// created if needed.
std::filesystem::path defaultLogFilePath();

} // namespace platform
