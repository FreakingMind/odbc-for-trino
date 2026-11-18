#include "gtest/gtest.h"

/*
DSN lookup through the driver's own copy of iodbcinst (macOS / iODBC).

iODBC's SQLGetPrivateProfileString only reads the user odbc.ini in the
default ODBC_BOTH_DSN mode, and resets the mode after every call. The driver
must find system DSNs (/Library/ODBC/odbc.ini, used by Excel for Mac) on its
own. The ini files are redirected with iODBC's ODBCINI (user) and SYSODBCINI
(system) environment variables, so the test never touches /Library.
*/
#ifdef __APPLE__

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include "../../../src/driver/config/profileReader.hpp"

#include <odbcinst.h>

namespace fs = std::filesystem;

namespace {

// Sets an environment variable for the lifetime of the object.
class ScopedEnv {
  public:
    ScopedEnv(const char* name, const std::string& value) : name_(name) {
      if (const char* old = std::getenv(name)) {
        original_ = std::string(old);
      }
      setenv(name, value.c_str(), 1);
    }

    ~ScopedEnv() {
      if (original_) {
        setenv(name_, original_->c_str(), 1);
      } else {
        unsetenv(name_);
      }
    }

    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

  private:
    const char* name_;
    std::optional<std::string> original_;
};

class ProfileReaderTest : public ::testing::Test {
  protected:
    void SetUp() override {
      std::string tmpl =
          (fs::temp_directory_path() / "trino-odbc-ini-XXXXXX").string();
      ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
      dir_ = tmpl;
      userIni_.emplace("ODBCINI", (dir_ / "user-odbc.ini").string());
      systemIni_.emplace("SYSODBCINI", (dir_ / "system-odbc.ini").string());
      writeFile("user-odbc.ini", "");
      writeFile("system-odbc.ini", "");
    }

    void TearDown() override {
      userIni_.reset();
      systemIni_.reset();
      std::error_code ec;
      fs::remove_all(dir_, ec);
    }

    void writeFile(const std::string& name, const std::string& content) {
      std::ofstream(dir_ / name) << content;
    }

    fs::path dir_;
    std::optional<ScopedEnv> userIni_;
    std::optional<ScopedEnv> systemIni_;
};

constexpr const char* SYSTEM_DSN = "[ODBC Data Sources]\n"
                                   "TrinoTest = Trino ODBC\n"
                                   "\n"
                                   "[TrinoTest]\n"
                                   "Driver     = Trino ODBC\n"
                                   "hostname   = https://system.example\n"
                                   "port       = 8443\n"
                                   "loglevel   = Debug\n"
                                   "sslCaFile  = /tmp/system-ca.pem\n";

} // namespace

TEST_F(ProfileReaderTest, ReadsSystemDsn) {
  writeFile("system-odbc.ini", SYSTEM_DSN);

  DriverConfig config = readDriverConfigFromProfile("TrinoTest");

  EXPECT_EQ(config.getHostname(), "https://system.example");
  EXPECT_EQ(config.getPortNum(), 8443);
  EXPECT_EQ(config.getLogLevelEnum(), LL_DEBUG);
  EXPECT_EQ(config.getSslCaFile(), "/tmp/system-ca.pem");
}

TEST_F(ProfileReaderTest, UserDsnWinsOverSystemDsn) {
  writeFile("system-odbc.ini", SYSTEM_DSN);
  writeFile("user-odbc.ini",
            "[TrinoTest]\n"
            "Driver   = Trino ODBC\n"
            "hostname = https://user.example\n");

  DriverConfig config = readDriverConfigFromProfile("TrinoTest");

  EXPECT_EQ(config.getHostname(), "https://user.example");
  // Keys missing in the user DSN take the defaults, never values from a
  // system DSN of the same name.
  EXPECT_EQ(config.getPortNum(), 8080);
  EXPECT_EQ(config.getSslCaFile(), "");
}

TEST_F(ProfileReaderTest, UnknownDsnUsesDefaults) {
  writeFile("system-odbc.ini", SYSTEM_DSN);

  DriverConfig config = readDriverConfigFromProfile("NoSuchDsn");

  EXPECT_EQ(config.getHostname(), "localhost");
  EXPECT_EQ(config.getPortNum(), 8080);
}

TEST_F(ProfileReaderTest, LeavesConfigModeAtDefault) {
  writeFile("system-odbc.ini", SYSTEM_DSN);

  readDriverConfigFromProfile("TrinoTest");

  UWORD mode = ODBC_USER_DSN;
  ASSERT_TRUE(SQLGetConfigMode(&mode));
  EXPECT_EQ(mode, ODBC_BOTH_DSN);
}

TEST_F(ProfileReaderTest, ReadsCatalogAndSchema) {
  writeFile("system-odbc.ini",
            "[ODBC Data Sources]\n"
            "TrinoTest = Trino ODBC\n"
            "\n"
            "[TrinoTest]\n"
            "hostname = https://system.example\n"
            "catalog  =  iceberg \n"
            "schema   = cdm_core\n");

  DriverConfig config = readDriverConfigFromProfile("TrinoTest");

  EXPECT_EQ(config.getCatalog(), "iceberg");
  EXPECT_EQ(config.getSchema(), "cdm_core");
  // They round-trip through a connection string.
  const DriverConfig copy = driverConfigFromKVPs(driverConfigToKVPs(config));
  EXPECT_EQ(DriverConfig(copy).getCatalog(), "iceberg");
  EXPECT_EQ(DriverConfig(copy).getSchema(), "cdm_core");
}

#endif // __APPLE__
