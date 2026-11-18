#include "gtest/gtest.h"
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include "../../../src/platform/platformPaths.hpp"

namespace fs = std::filesystem;

TEST(PlatformPathsTest, TokenCacheFilePath) {
  fs::path path = platform::tokenCacheFilePath();
  ASSERT_FALSE(path.empty());
  EXPECT_TRUE(path.is_absolute()) << path;
  EXPECT_EQ(path.filename(), fs::path("TrinoODBCTokenCache.json"));
  EXPECT_TRUE(fs::is_directory(path.parent_path())) << path.parent_path();
}

TEST(PlatformPathsTest, DefaultLogFilePath) {
  fs::path path = platform::defaultLogFilePath();
  ASSERT_FALSE(path.empty());
  EXPECT_TRUE(path.is_absolute()) << path;
#ifdef _WIN32
  EXPECT_EQ(path.filename(), fs::path("odbclog.txt"));
#else
  EXPECT_EQ(path.filename(), fs::path("TrinoODBC.log"));
#endif
  EXPECT_TRUE(fs::is_directory(path.parent_path())) << path.parent_path();
}

#ifdef __APPLE__

namespace {

// Points $HOME at a fresh temporary directory for the lifetime of the
// object, then restores the original value and removes the directory.
class ScopedHome {
  public:
    ScopedHome() {
      if (const char* home = std::getenv("HOME")) {
        original_ = std::string(home);
      }
      std::string tmpl =
          (fs::temp_directory_path() / "trino-odbc-home-XXXXXX").string();
      char* dir = mkdtemp(tmpl.data());
      if (dir) {
        // Resolve symlinks (/var -> /private/var) so comparisons are exact.
        path_ = fs::canonical(dir);
        setenv("HOME", path_.c_str(), 1);
      }
    }

    ~ScopedHome() {
      if (original_) {
        setenv("HOME", original_->c_str(), 1);
      } else {
        unsetenv("HOME");
      }
      if (!path_.empty()) {
        std::error_code ec;
        fs::remove_all(path_, ec);
      }
    }

    const fs::path& path() const {
      return path_;
    }

  private:
    std::optional<std::string> original_;
    fs::path path_;
};

fs::path homeDir() {
  const char* home = std::getenv("HOME");
  return home ? fs::path(home) : fs::path();
}

} // namespace

TEST(PlatformPathsTest, MacTokenCacheUnderApplicationSupport) {
  fs::path home = homeDir();
  ASSERT_FALSE(home.empty());
  EXPECT_EQ(platform::tokenCacheFilePath(),
            home / "Library" / "Application Support" / "TrinoODBC" /
                "TrinoODBCTokenCache.json");
}

TEST(PlatformPathsTest, MacLogUnderLibraryLogs) {
  fs::path home = homeDir();
  ASSERT_FALSE(home.empty());
  EXPECT_EQ(platform::defaultLogFilePath(),
            home / "Library" / "Logs" / "TrinoODBC" / "TrinoODBC.log");
}

TEST(PlatformPathsTest, MacPathsFollowHomeLikeExcelSandbox) {
  // Inside Excel's sandbox $HOME points into the app container; the driver
  // must derive its paths from $HOME rather than the passwd entry.
  ScopedHome sandbox;
  ASSERT_FALSE(sandbox.path().empty()) << "mkdtemp failed";

  fs::path tokenPath = platform::tokenCacheFilePath();
  EXPECT_EQ(tokenPath,
            sandbox.path() / "Library" / "Application Support" / "TrinoODBC" /
                "TrinoODBCTokenCache.json");
  EXPECT_TRUE(fs::is_directory(tokenPath.parent_path()));

  fs::path logPath = platform::defaultLogFilePath();
  EXPECT_EQ(logPath,
            sandbox.path() / "Library" / "Logs" / "TrinoODBC" /
                "TrinoODBC.log");
  EXPECT_TRUE(fs::is_directory(logPath.parent_path()));
}

TEST(PlatformPathsTest, MacEmptyHomeFallsBackToPasswd) {
  ScopedHome sandbox; // restores $HOME afterwards
  setenv("HOME", "", 1);
  fs::path tokenPath = platform::tokenCacheFilePath();
  ASSERT_FALSE(tokenPath.empty());
  EXPECT_TRUE(tokenPath.is_absolute());
  EXPECT_EQ(tokenPath.filename(), fs::path("TrinoODBCTokenCache.json"));
}

#endif // __APPLE__
