#include "profileReader.hpp"

#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstdint>
#include <mutex>
#include <odbcinst.h>
#include <optional>
#include <string>
#include <vector>

#include "../../util/cryptUtils.hpp"
#include "../../util/writeLog.hpp"
#include "driverConfig.hpp"


namespace {

// A value no real setting has: tells "key missing" from "key empty".
constexpr const char* MISSING_SENTINEL = "\x01<missing>";

/*
Which odbc.ini holds a DSN, as an iodbcinst config mode.

In the default ODBC_BOTH_DSN mode iODBC's SQLGetPrivateProfileString reads
the user odbc.ini only (a key missing there may even be taken from a system
DSN of the same name), and it resets the mode to ODBC_BOTH_DSN after every
call. iODBC's driver manager selects the mode before loading a driver, but
on macOS the driver links its own private copy of iodbcinst, which never
sees that: without this, every setting of a system DSN
(/Library/ODBC/odbc.ini, the one Excel uses) would silently fall back to its
default. So the driver finds the DSN itself (user file first, like the
driver managers do) and selects that file before each read.
*/
std::optional<UWORD> dsnConfigMode = std::nullopt;

void selectDsnConfigMode() {
  if (dsnConfigMode) {
    SQLSetConfigMode(*dsnConfigMode);
  }
}

bool dsnHasDriverKey(const std::string& dsn) {
  char value[64];
  SQLGetPrivateProfileString(dsn.c_str(),
                             "Driver",
                             MISSING_SENTINEL,
                             value,
                             sizeof(value),
                             "ODBC.INI");
  return std::string(value) != MISSING_SENTINEL;
}

std::optional<UWORD> findDsnConfigMode(const std::string& dsn) {
  for (const UWORD mode : {static_cast<UWORD>(ODBC_USER_DSN),
                           static_cast<UWORD>(ODBC_SYSTEM_DSN)}) {
    if (SQLSetConfigMode(mode) && dsnHasDriverKey(dsn)) {
      WriteLog(LL_DEBUG,
               std::string("  DSN found in the ") +
                   (mode == ODBC_USER_DSN ? "user" : "system") + " odbc.ini");
      return mode;
    }
  }
  // No Driver key in either file (a hand-made DSN, or none at all): read
  // the way the driver managers do by default. The mode must be reset
  // explicitly: Windows keeps the last one set (ODBC_SYSTEM_DSN from the
  // loop above), which would hide a user DSN.
  WriteLog(LL_WARN, "  DSN has no Driver key in odbc.ini: " + dsn);
  return static_cast<UWORD>(ODBC_BOTH_DSN);
}

// The config mode is process-wide iodbcinst state.
std::mutex profileMutex;

} // namespace

std::string readFromPrivateProfile(std::string dsn, std::string key) {
  std::string defaultVal = "";
  if (DRIVER_CONFIG_DEFAULT_VALUES.count(key)) {
    defaultVal = DRIVER_CONFIG_DEFAULT_VALUES.at(key);
  }

  // Values can be long (access tokens with many claims run to several KB):
  // grow the buffer while a value fills it, instead of cutting it off.
  std::vector<char> value(4096);
  for (;;) {
    selectDsnConfigMode();
    const int length =
        SQLGetPrivateProfileString(dsn.c_str(),
                                   key.c_str(),
                                   defaultVal.c_str(),
                                   value.data(),
                                   static_cast<int>(value.size()),
                                   "ODBC.INI");
    if (length < static_cast<int>(value.size()) - 1 ||
        value.size() >= 1024 * 1024) {
      break;
    }
    value.assign(value.size() * 4, '\0');
  }
  value.back() = '\0';
  return std::string(value.data());
}


DriverConfig readDriverConfigFromProfile(std::string dsn) {
  const std::lock_guard<std::mutex> lock(profileMutex);
  UWORD previousMode = ODBC_BOTH_DSN;
  SQLGetConfigMode(&previousMode);
  dsnConfigMode = findDsnConfigMode(dsn);
  // Restore the caller's mode once this DSN is read, also on exceptions.
  struct RestoreConfigMode {
      UWORD mode;
      ~RestoreConfigMode() {
        dsnConfigMode = std::nullopt;
        SQLSetConfigMode(mode);
      }
  } restoreConfigMode{previousMode};

  // The driver config reads values assuming mostly camelCase names.
  DriverConfig config;
  config.setDSN(dsn);
  config.setHostname(readFromPrivateProfile(dsn, "hostname"));
  config.setPort(readFromPrivateProfile(dsn, "port"));
  config.setLogLevel(readFromPrivateProfile(dsn, "loglevel"));
  config.setAuthMethod(readFromPrivateProfile(dsn, "authmethod"));
  config.setOidcDiscoveryUrl(readFromPrivateProfile(dsn, "oidcDiscoveryUrl"));
  config.setClientId(readFromPrivateProfile(dsn, "clientId"));
  config.setOidcScope(readFromPrivateProfile(dsn, "oidcScope"));
  config.setSslVerification(readFromPrivateProfile(dsn, "sslVerification"));
  config.setSslCaFile(readFromPrivateProfile(dsn, "sslCaFile"));
  config.setAnsiEncoding(readFromPrivateProfile(dsn, "ansiEncoding"));
  config.setCatalog(readFromPrivateProfile(dsn, "catalog"));
  config.setSchema(readFromPrivateProfile(dsn, "schema"));
  config.setUser(readFromPrivateProfile(dsn, "user"));
  // Stored in clear text: prefer letting the application ask for them.
  config.setPassword(readFromPrivateProfile(dsn, "password"));
  config.setAccessToken(readFromPrivateProfile(dsn, "accessToken"));
  config.setTokenLifetime(readFromPrivateProfile(dsn, "tokenLifetime"));

  std::string secretEncryptionLevel =
      readFromPrivateProfile(dsn, "secretEncryptionLevel");
  std::string encryptedClientSecret =
      readFromPrivateProfile(dsn, "encryptedClientSecret");

  // The Windows DSN dialog stores the secret encrypted. Where DSNs are
  // edited by hand (odbc.ini on macOS) a plain clientSecret is accepted too;
  // the encrypted value wins if both are present.
  if (encryptedClientSecret.empty()) {
    config.setClientSecret(readFromPrivateProfile(dsn, "clientSecret"));
  } else if (secretEncryptionLevel == "user") {
    config.setClientSecret(userDecryptString(encryptedClientSecret));
  } else if (secretEncryptionLevel == "system") {
    config.setClientSecret(systemDecryptString(encryptedClientSecret));
  } else {
    throw std::runtime_error("Unknown secret encryption level: " +
                             secretEncryptionLevel);
  }

  return config;
}
