#include "../../src/util/windowsLean.hpp"

#include <sql.h>
#include <sqlext.h>

#include <curl/curl.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../src/platform/systemTrustStore.hpp"
#include "../../src/platform/userName.hpp"

/*
Integration tests for the authentication / TLS options against the Docker
test environment (docker/compose.yml, docker/setup.sh).

Everything environment-specific is read at runtime from the files generated
by docker/setup.sh, located relative to $ODBCINI:
  - $ODBCINI                    -> DSN definitions (docker/.generated/odbc.ini)
  - dirname($ODBCINI)/server.crt -> trino-secure's self-signed certificate
  - dirname($ODBCINI)/../test.env -> client credentials (KEY=VALUE)

The tests skip when ODBCINI is unset or trino-secure isn't reachable, so the
suite stays green without Docker.
*/

namespace {

const std::string CLIENT_CRED_DSN = "TrinoSecureClientCred";
const std::string NO_AUTH_DSN     = "TrinoSecureNoAuth";
const std::string UNTRUSTED_DSN   = "TrinoSecureUntrusted";
const std::string PASSWORD_DSN    = "TrinoSecurePassword";
// trino-noauth over plain HTTP.
const std::string PLAIN_NO_AUTH_DSN = "TrinoNoAuth";

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

std::string trimmed(const std::string& s) {
  const char* ws = " \t\r\n";
  size_t begin   = s.find_first_not_of(ws);
  if (begin == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(ws);
  return s.substr(begin, end - begin + 1);
}

std::optional<std::string> getEnv(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

void setEnv(const char* name, const std::optional<std::string>& value) {
#ifdef _WIN32
  _putenv_s(name, value ? value->c_str() : "");
#else
  if (value) {
    setenv(name, value->c_str(), 1);
  } else {
    unsetenv(name);
  }
#endif
}

// Parses a KEY=VALUE file (docker/test.env). Blank lines and # comments are
// ignored.
std::map<std::string, std::string>
readEnvFile(const std::filesystem::path& path) {
  std::map<std::string, std::string> values;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    line = trimmed(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    values[trimmed(line.substr(0, eq))] = trimmed(line.substr(eq + 1));
  }
  return values;
}

// Reads one key of one section from an odbc.ini-style file. Keys are
// compared case-insensitively, like the driver manager does.
std::string readIniValue(const std::filesystem::path& path,
                         const std::string& section,
                         const std::string& key) {
  std::ifstream in(path);
  std::string line;
  bool inSection = false;
  while (std::getline(in, line)) {
    line = trimmed(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      inSection = line.substr(1, line.size() - 2) == section;
      continue;
    }
    size_t eq = line.find('=');
    if (inSection && eq != std::string::npos &&
        toLower(trimmed(line.substr(0, eq))) == toLower(key)) {
      return trimmed(line.substr(eq + 1));
    }
  }
  return "";
}

// Probes https://localhost:8443/v1/info (certificate not checked: this only
// asks "is something listening"). The result is computed once per process.
bool secureServerReachable() {
  static const bool reachable = [] {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8443/v1/info");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
  }();
  return reachable;
}

} // namespace

class AuthFlowsTest : public ::testing::Test {
  protected:
    SQLHENV hEnv = nullptr;
    // All connection handles allocated by connect(), connected or not.
    std::vector<SQLHDBC> connected;
    std::vector<SQLHSTMT> statements;

    std::filesystem::path generatedDir;
    std::filesystem::path caFile;
    std::map<std::string, std::string> testEnv;

    // Saved TRINO_ODBC_TOKEN_CACHE, restored after each test.
    std::optional<std::string> savedTokenCacheSetting;
    bool tokenCacheDisabled = false;

    void SetUp() override {
      std::optional<std::string> odbcIni = getEnv("ODBCINI");
      if (!odbcIni) {
        GTEST_SKIP() << "ODBCINI is not set; point it at "
                        "docker/.generated/odbc.ini to run the auth "
                        "integration tests";
      }
      generatedDir = std::filesystem::path(*odbcIni).parent_path();
      caFile       = generatedDir / "server.crt";
      testEnv      = readEnvFile(generatedDir.parent_path() / "test.env");

      if (readIniValue(*odbcIni, CLIENT_CRED_DSN, "hostname").empty()) {
        GTEST_SKIP() << "DSN " << CLIENT_CRED_DSN << " not found in "
                     << *odbcIni << "; run docker/setup.sh";
      }
      if (!secureServerReachable()) {
        GTEST_SKIP() << "trino-secure is not reachable at "
                        "https://localhost:8443; start docker/compose.yml";
      }

      disableTokenCache();

      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
    }

    void TearDown() override {
      for (SQLHSTMT hStmt : statements) {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
      }
      // Includes handles whose connect failed: freeing those must work too.
      for (SQLHDBC hDbc : connected) {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
      }
      if (hEnv) {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
      }
      restoreTokenCache();
    }

    // The persistent token cache encrypts tokens with a key kept in the
    // macOS keychain, which can pop up system dialogs for the unsigned test
    // binary. Disable it (no file, no keychain) so the tests neither prompt
    // nor touch the user's real cache.
    void disableTokenCache() {
      savedTokenCacheSetting = getEnv("TRINO_ODBC_TOKEN_CACHE");
      setEnv("TRINO_ODBC_TOKEN_CACHE", std::string("none"));
      tokenCacheDisabled = true;
    }

    void restoreTokenCache() {
      if (!tokenCacheDisabled) {
        return;
      }
      setEnv("TRINO_ODBC_TOKEN_CACHE", savedTokenCacheSetting);
      tokenCacheDisabled = false;
    }

    std::string envValue(const std::string& key) const {
      auto it = testEnv.find(key);
      return it == testEnv.end() ? "" : it->second;
    }

    // Client credentials passed in the connection string, read from the
    // generated config so nothing is hardcoded here.
    std::string clientCredConnStr() const {
      std::string odbcIni = *getEnv("ODBCINI");
      return "authmethod=OIDC Client Cred Auth;oidcdiscoveryurl=" +
             readIniValue(odbcIni, CLIENT_CRED_DSN, "oidcDiscoveryUrl") +
             ";clientid=" + envValue("ODBC_CLIENT_CREDENTIALS_ID") +
             ";clientsecret=" + envValue("ODBC_CLIENT_CREDENTIALS_SECRET") +
             ";";
    }

    SQLRETURN
    connect(const std::string& dsn, const std::string& extra, SQLHDBC* outDbc) {
      SQLHDBC hDbc  = nullptr;
      SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
      if (ret != SQL_SUCCESS) {
        return ret;
      }
      *outDbc = hDbc;
      connected.push_back(hDbc);

      std::string connStr = "DSN=" + dsn + ";" + extra;
      SQLSMALLINT outLen  = 0;
      ret = SQLDriverConnect(hDbc,
                             nullptr,
                             (SQLCHAR*)connStr.c_str(),
                             static_cast<SQLSMALLINT>(connStr.size()),
                             nullptr,
                             0,
                             &outLen,
                             SQL_DRIVER_NOPROMPT);
      return ret;
    }

    SQLHSTMT allocStatement(SQLHDBC hDbc) {
      SQLHSTMT hStmt = nullptr;
      EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt), SQL_SUCCESS);
      if (hStmt) {
        statements.push_back(hStmt);
      }
      return hStmt;
    }

    static std::string diagMessage(SQLSMALLINT handleType, SQLHANDLE handle) {
      SQLCHAR sqlState[6]    = {0};
      SQLCHAR message[4096]  = {0};
      SQLINTEGER nativeError = 0;
      SQLSMALLINT msgLen     = 0;
      SQLRETURN ret          = SQLGetDiagRec(handleType,
                                             handle,
                                             1,
                                             sqlState,
                                             &nativeError,
                                             message,
                                             sizeof(message),
                                             &msgLen);
      if (!SQL_SUCCEEDED(ret)) {
        return "";
      }
      return std::string(reinterpret_cast<char*>(message));
    }

    // Runs a single-value query and returns the first column of the first
    // row. Fails the test (and returns "") on any error.
    std::string querySingleValue(SQLHDBC hDbc, const std::string& sql) {
      SQLHSTMT hStmt = allocStatement(hDbc);
      SQLRETURN ret  = SQLExecDirect(hStmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
      EXPECT_TRUE(SQL_SUCCEEDED(ret))
          << "SQLExecDirect failed: " << diagMessage(SQL_HANDLE_STMT, hStmt);
      if (!SQL_SUCCEEDED(ret)) {
        return "";
      }
      ret = SQLFetch(hStmt);
      EXPECT_TRUE(SQL_SUCCEEDED(ret))
          << "SQLFetch failed: " << diagMessage(SQL_HANDLE_STMT, hStmt);
      if (!SQL_SUCCEEDED(ret)) {
        return "";
      }
      char buf[512]    = {0};
      SQLLEN indicator = 0;
      ret = SQLGetData(hStmt, 1, SQL_C_CHAR, buf, sizeof(buf), &indicator);
      EXPECT_TRUE(SQL_SUCCEEDED(ret))
          << "SQLGetData failed: " << diagMessage(SQL_HANDLE_STMT, hStmt);
      if (!SQL_SUCCEEDED(ret) || indicator == SQL_NULL_DATA) {
        return "";
      }
      return std::string(buf);
    }

    static std::string diagState(SQLSMALLINT handleType, SQLHANDLE handle) {
      SQLCHAR sqlState[6]    = {0};
      SQLCHAR message[1024]  = {0};
      SQLINTEGER nativeError = 0;
      SQLSMALLINT msgLen     = 0;
      SQLRETURN ret          = SQLGetDiagRec(handleType,
                                             handle,
                                             1,
                                             sqlState,
                                             &nativeError,
                                             message,
                                             sizeof(message),
                                             &msgLen);
      if (!SQL_SUCCEEDED(ret)) {
        return "";
      }
      return std::string(reinterpret_cast<char*>(sqlState));
    }

    // An access token for the client-credentials client, straight from
    // Keycloak's token endpoint. Empty (and a test failure) on error.
    std::string fetchAccessToken() {
      std::string discovery =
          readIniValue(*getEnv("ODBCINI"), CLIENT_CRED_DSN, "oidcDiscoveryUrl");
      const std::string suffix = "/.well-known/openid-configuration";
      EXPECT_TRUE(discovery.ends_with(suffix)) << discovery;
      std::string tokenUrl =
          discovery.substr(0, discovery.size() - suffix.size()) +
          "/protocol/openid-connect/token";

      CURL* curl = curl_easy_init();
      if (curl == nullptr) {
        ADD_FAILURE() << "curl_easy_init failed";
        return "";
      }
      std::string form = "grant_type=client_credentials";
      for (const auto& [name, key] :
           {std::pair{"client_id", "ODBC_CLIENT_CREDENTIALS_ID"},
            std::pair{"client_secret", "ODBC_CLIENT_CREDENTIALS_SECRET"}}) {
        char* escaped = curl_easy_escape(curl, envValue(key).c_str(), 0);
        form += std::string("&") + name + "=" + escaped;
        curl_free(escaped);
      }
      std::string body;
      curl_easy_setopt(curl, CURLOPT_URL, tokenUrl.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
      curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
      curl_easy_setopt(
          curl,
          CURLOPT_WRITEFUNCTION,
          +[](char* data, size_t size, size_t count, void* out) {
            static_cast<std::string*>(out)->append(data, size * count);
            return size * count;
          });
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
      const CURLcode res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);
      EXPECT_EQ(res, CURLE_OK) << tokenUrl;
      nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
      if (!json.is_object() || !json.contains("access_token")) {
        ADD_FAILURE() << "no access_token from " << tokenUrl << ": " << body;
        return "";
      }
      return json["access_token"].get<std::string>();
    }

    std::string clientCredentialsUser() const {
      return "service-account-" + envValue("ODBC_CLIENT_CREDENTIALS_ID");
    }

    // Executes a query that is expected to fail and returns the statement's
    // diagnostic message.
    std::string expectExecutionFailure(SQLHDBC hDbc) {
      SQLHSTMT hStmt = allocStatement(hDbc);
      SQLRETURN ret =
          SQLExecDirect(hStmt, (SQLCHAR*)"SELECT current_user", SQL_NTS);
      EXPECT_EQ(ret, SQL_ERROR);
      return diagMessage(SQL_HANDLE_STMT, hStmt);
    }

    void connectOrFail(const std::string& dsn,
                       const std::string& extra,
                       SQLHDBC* hDbc) {
      SQLRETURN ret = connect(dsn, extra, hDbc);
      ASSERT_EQ(ret, SQL_SUCCESS)
          << "SQLDriverConnect(" << dsn
          << ") failed: " << diagMessage(SQL_HANDLE_DBC, *hDbc);
    }
};

TEST_F(AuthFlowsTest, ClientCredentialsFromDsn) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(CLIENT_CRED_DSN, "", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            "service-account-" + envValue("ODBC_CLIENT_CREDENTIALS_ID"));
}

TEST_F(AuthFlowsTest, SslVerificationNoneWithoutCaFile) {
  // Override the DSN: no CA file, no certificate verification.
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(CLIENT_CRED_DSN, "sslcafile=;sslverification=NONE", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            "service-account-" + envValue("ODBC_CLIENT_CREDENTIALS_ID"));
}

TEST_F(AuthFlowsTest, SslVerificationNoneOnUntrustedDsnReachesAuth) {
  // TLS succeeds once verification is off, so the failure is now the missing
  // credentials rather than the certificate.
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(UNTRUSTED_DSN, "sslverification=NONE", &hDbc));
  std::string message = expectExecutionFailure(hDbc);
  EXPECT_NE(message.find("401"), std::string::npos) << message;
}

TEST_F(AuthFlowsTest, CaFileAndClientCredentialsFromConnectionString) {
  ASSERT_FALSE(envValue("ODBC_CLIENT_CREDENTIALS_ID").empty())
      << "ODBC_CLIENT_CREDENTIALS_ID missing from test.env";
  ASSERT_TRUE(std::filesystem::exists(caFile)) << caFile;

  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(UNTRUSTED_DSN,
                    "sslcafile=" + caFile.string() + ";" + clientCredConnStr(),
                    &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            "service-account-" + envValue("ODBC_CLIENT_CREDENTIALS_ID"));
}

TEST_F(AuthFlowsTest, NoCredentialsFailsWith401) {
  // Connecting is lazy: SQLDriverConnect succeeds, the first request fails.
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(NO_AUTH_DSN, "", &hDbc));
  std::string message = expectExecutionFailure(hDbc);
  EXPECT_NE(message.find("401"), std::string::npos) << message;
}

TEST_F(AuthFlowsTest, UntrustedCertificateFails) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(UNTRUSTED_DSN, "", &hDbc));
  std::string message = expectExecutionFailure(hDbc);
  EXPECT_NE(toLower(message).find("certificate"), std::string::npos) << message;
}

// The default trust store on macOS is handed to libcurl as an in-memory CA
// bundle (CURLOPT_CAINFO_BLOB). Checks that this libcurl build accepts one
// and verifies against it: trusted with the server's certificate, rejected
// with the system store (the test certificate is self-signed).
TEST_F(AuthFlowsTest, CurlVerifiesAgainstInMemoryCaBundles) {
  std::ifstream in(caFile, std::ios::binary);
  ASSERT_TRUE(in) << caFile;
  std::string serverCert((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

  const auto fetchInfo = [](const std::string& pem) {
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8443/v1/info");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_blob blob{};
    blob.data  = const_cast<char*>(pem.data());
    blob.len   = pem.size();
    blob.flags = CURL_BLOB_COPY;
    EXPECT_EQ(curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob), CURLE_OK);
    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res;
  };

  EXPECT_EQ(fetchInfo(serverCert), CURLE_OK);
  const platform::SystemTrustStore& store = platform::systemTrustStore();
  if (!store.pem.empty()) {
    EXPECT_EQ(fetchInfo(store.pem), CURLE_PEER_FAILED_VERIFICATION);
  }
}

TEST_F(AuthFlowsTest, UntrustedCertificateFailsOnPrepare) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(UNTRUSTED_DSN, "", &hDbc));
  SQLHSTMT hStmt = allocStatement(hDbc);
  SQLRETURN ret  = SQLPrepare(hStmt, (SQLCHAR*)"SELECT current_user", SQL_NTS);
  if (SQL_SUCCEEDED(ret)) {
    // A driver may defer the round trip to SQLExecute.
    ret = SQLExecute(hStmt);
  }
  EXPECT_EQ(ret, SQL_ERROR);
  std::string message = diagMessage(SQL_HANDLE_STMT, hStmt);
  EXPECT_NE(toLower(message).find("certificate"), std::string::npos) << message;
}

TEST_F(AuthFlowsTest, InvalidSslVerificationRejected) {
  // Used to escape SQLDriverConnect as a C++ exception, which terminates the
  // host application.
  SQLHDBC hDbc  = nullptr;
  SQLRETURN ret = connect(CLIENT_CRED_DSN, "sslverification=maybe", &hDbc);
  EXPECT_EQ(ret, SQL_ERROR);
  EXPECT_NE(toLower(diagMessage(SQL_HANDLE_DBC, hDbc)).find("sslverification"),
            std::string::npos)
      << diagMessage(SQL_HANDLE_DBC, hDbc);
}

TEST_F(AuthFlowsTest, FreeConnectionHandleAfterFailedConnect) {
  // An application frees the connection handle after a failed connect.
  SQLHDBC hDbc = nullptr;
  ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc), SQL_SUCCESS);
  std::string connStr = "DSN=" + CLIENT_CRED_DSN + ";port=0;";
  SQLRETURN ret       = SQLDriverConnect(hDbc,
                                         nullptr,
                                         (SQLCHAR*)connStr.c_str(),
                                         SQL_NTS,
                                         nullptr,
                                         0,
                                         nullptr,
                                         SQL_DRIVER_NOPROMPT);
  ASSERT_EQ(ret, SQL_ERROR) << "port=0 is expected to be rejected";

  // Used to crash: the connection config is never created when connecting
  // fails.
  EXPECT_EQ(SQLFreeHandle(SQL_HANDLE_DBC, hDbc), SQL_SUCCESS);
}

TEST_F(AuthFlowsTest, ConsecutiveClientCredentialsConnections) {
  std::string expectedUser =
      "service-account-" + envValue("ODBC_CLIENT_CREDENTIALS_ID");

  // Two connections in a row, the first one still open: each must
  // authenticate and run queries independently.
  SQLHDBC first = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(CLIENT_CRED_DSN, "", &first));
  EXPECT_EQ(querySingleValue(first, "SELECT current_user"), expectedUser);

  SQLHDBC second = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(CLIENT_CRED_DSN, "", &second));
  EXPECT_EQ(querySingleValue(second, "SELECT current_user"), expectedUser);
  EXPECT_EQ(querySingleValue(first, "SELECT 1"), "1");
}

// ---------------------------------------------------------------------------
// Password, access token and the user of "No Auth"
// ---------------------------------------------------------------------------

TEST_F(AuthFlowsTest, PasswordFromConnectionString) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(
      PASSWORD_DSN, "PWD=" + envValue("TRINO_TEST_PASSWORD") + ";", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            envValue("TRINO_TEST_USER"));
}

TEST_F(AuthFlowsTest, PasswordWithUidOverridingTheDsn) {
  // Excel's login prompt passes UID and PWD.
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(NO_AUTH_DSN,
                    "authmethod=Password;UID=" + envValue("TRINO_TEST_USER") +
                        ";PWD=" + envValue("TRINO_TEST_PASSWORD") + ";",
                    &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            envValue("TRINO_TEST_USER"));
}

TEST_F(AuthFlowsTest, PasswordThroughSqlConnect) {
  SQLHDBC hDbc = nullptr;
  ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc), SQL_SUCCESS);
  connected.push_back(hDbc);
  const std::string user     = envValue("TRINO_TEST_USER");
  const std::string password = envValue("TRINO_TEST_PASSWORD");
  SQLRETURN ret              = SQLConnect(hDbc,
                                          (SQLCHAR*)PASSWORD_DSN.c_str(),
                                          SQL_NTS,
                                          (SQLCHAR*)user.c_str(),
                                          SQL_NTS,
                                          (SQLCHAR*)password.c_str(),
                                          SQL_NTS);
  ASSERT_EQ(ret, SQL_SUCCESS) << diagMessage(SQL_HANDLE_DBC, hDbc);
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"), user);
}

TEST_F(AuthFlowsTest, WrongPasswordFailsWith28000) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(PASSWORD_DSN, "PWD=wrong;", &hDbc));
  SQLHSTMT hStmt = allocStatement(hDbc);
  EXPECT_EQ(SQLExecDirect(hStmt, (SQLCHAR*)"SELECT 1", SQL_NTS), SQL_ERROR);
  EXPECT_EQ(diagState(SQL_HANDLE_STMT, hStmt), "28000")
      << diagMessage(SQL_HANDLE_STMT, hStmt);
  EXPECT_NE(diagMessage(SQL_HANDLE_STMT, hStmt).find("401"), std::string::npos);
}

TEST_F(AuthFlowsTest, MissingPasswordFailsOnConnect) {
  SQLHDBC hDbc  = nullptr;
  SQLRETURN ret = connect(PASSWORD_DSN, "", &hDbc);
  EXPECT_EQ(ret, SQL_ERROR);
  EXPECT_EQ(diagState(SQL_HANDLE_DBC, hDbc), "28000")
      << diagMessage(SQL_HANDLE_DBC, hDbc);
}

TEST_F(AuthFlowsTest, PasswordNeverSentOverHttp) {
  SQLHDBC hDbc  = nullptr;
  SQLRETURN ret = connect(
      PLAIN_NO_AUTH_DSN, "authmethod=Password;UID=alice;PWD=secret;", &hDbc);
  EXPECT_EQ(ret, SQL_ERROR);
  EXPECT_EQ(diagState(SQL_HANDLE_DBC, hDbc), "28000");
  EXPECT_NE(diagMessage(SQL_HANDLE_DBC, hDbc).find("HTTPS"), std::string::npos)
      << diagMessage(SQL_HANDLE_DBC, hDbc);
}

TEST_F(AuthFlowsTest, AccessTokenFromConnectionString) {
  const std::string token = fetchAccessToken();
  ASSERT_FALSE(token.empty());
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(NO_AUTH_DSN,
                    "authmethod=Access Token;accesstoken=" + token + ";",
                    &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            clientCredentialsUser());
}

TEST_F(AuthFlowsTest, AccessTokenInPwd) {
  const std::string token = fetchAccessToken();
  ASSERT_FALSE(token.empty());
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(
      NO_AUTH_DSN, "authmethod=access token;PWD=" + token + ";", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"),
            clientCredentialsUser());
}

TEST_F(AuthFlowsTest, InvalidAccessTokenFailsWith28000) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(
      NO_AUTH_DSN, "authmethod=Access Token;accesstoken=not-a-token;", &hDbc));
  SQLHSTMT hStmt = allocStatement(hDbc);
  EXPECT_EQ(SQLExecDirect(hStmt, (SQLCHAR*)"SELECT 1", SQL_NTS), SQL_ERROR);
  EXPECT_EQ(diagState(SQL_HANDLE_STMT, hStmt), "28000")
      << diagMessage(SQL_HANDLE_STMT, hStmt);
}

TEST_F(AuthFlowsTest, NoAuthSendsConfiguredUser) {
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(
      connectOrFail(PLAIN_NO_AUTH_DSN, "user=odbc-test-user;", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"), "odbc-test-user");
}

TEST_F(AuthFlowsTest, NoAuthDefaultsToLoginName) {
  const std::string login = platform::currentUserName();
  ASSERT_FALSE(login.empty());
  SQLHDBC hDbc = nullptr;
  ASSERT_NO_FATAL_FAILURE(connectOrFail(PLAIN_NO_AUTH_DSN, "", &hDbc));
  EXPECT_EQ(querySingleValue(hDbc, "SELECT current_user"), login);
}

TEST_F(AuthFlowsTest, UnreachableServerReports08S01) {
  SQLHDBC hDbc = nullptr;
  // Nothing listens on port 9 (discard) on the test machine.
  ASSERT_NO_FATAL_FAILURE(connectOrFail(PLAIN_NO_AUTH_DSN, "port=9;", &hDbc));
  SQLHSTMT hStmt = allocStatement(hDbc);
  EXPECT_EQ(SQLExecDirect(hStmt, (SQLCHAR*)"SELECT 1", SQL_NTS), SQL_ERROR);
  EXPECT_EQ(diagState(SQL_HANDLE_STMT, hStmt), "08S01")
      << diagMessage(SQL_HANDLE_STMT, hStmt);
}
