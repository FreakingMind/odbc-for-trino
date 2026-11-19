#include "../../src/util/windowsLean.hpp"

#include <sql.h>
#include <sqlext.h>

#include <curl/curl.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

/*
Token renewal against test/replay/fake_trino_oauth.py, a stand-in Trino
with External Authentication that issues tokens without readable claims and
can revoke them. ctest runs this suite through the script ("token_renewal");
it skips when FAKE_TRINO_PORT isn't set.
*/

namespace {

std::optional<std::string> envValue(const char* name) {
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

size_t appendBody(char* data, size_t size, size_t count, void* out) {
  static_cast<std::string*>(out)->append(data, size * count);
  return size * count;
}

} // namespace

class TokenRenewalTest : public ::testing::Test {
  protected:
    std::string port;
    SQLHENV hEnv = nullptr;
    SQLHDBC hDbc = nullptr;
    std::optional<std::string> savedTokenCache;

    void SetUp() override {
      std::optional<std::string> fakePort = envValue("FAKE_TRINO_PORT");
      if (!fakePort) {
        GTEST_SKIP() << "FAKE_TRINO_PORT is not set: run through ctest "
                        "(token_renewal) or test/replay/fake_trino_oauth.py";
      }
      port = *fakePort;
      // Keep tokens in memory: no cache file, no keychain prompts.
      savedTokenCache = envValue("TRINO_ODBC_TOKEN_CACHE");
      setEnv("TRINO_ODBC_TOKEN_CACHE", std::string("none"));
      control("POST", "/control/reset");

      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc), SQL_SUCCESS);
    }

    void TearDown() override {
      if (hDbc) {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
      }
      if (hEnv) {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
      }
      if (!port.empty()) {
        setEnv("TRINO_ODBC_TOKEN_CACHE", savedTokenCache);
      }
    }

    // Calls a control endpoint of the fake server and returns the body.
    std::string control(const char* method, const std::string& path) {
      CURL* curl = curl_easy_init();
      std::string body;
      const std::string url = "http://127.0.0.1:" + port + path;
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
      curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
      EXPECT_EQ(curl_easy_perform(curl), CURLE_OK) << url;
      curl_easy_cleanup(curl);
      return body;
    }

    int stat(const char* name) {
      nlohmann::json stats =
          nlohmann::json::parse(control("GET", "/control/stats"));
      return stats.at(name).get<int>();
    }

    void connect(const std::string& extra = "") {
      const std::string connStr = "hostname=http://127.0.0.1;port=" + port +
                                  ";authmethod=External Auth;" + extra;
      SQLRETURN ret             = SQLDriverConnect(hDbc,
                                                   nullptr,
                                                   (SQLCHAR*)connStr.c_str(),
                                                   SQL_NTS,
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   SQL_DRIVER_NOPROMPT);
      ASSERT_EQ(ret, SQL_SUCCESS) << diag(SQL_HANDLE_DBC, hDbc);
    }

    static std::string diag(SQLSMALLINT type, SQLHANDLE handle) {
      SQLCHAR state[6]       = {0};
      SQLCHAR message[2048]  = {0};
      SQLINTEGER nativeError = 0;
      SQLSMALLINT length     = 0;
      if (!SQL_SUCCEEDED(SQLGetDiagRec(type,
                                       handle,
                                       1,
                                       state,
                                       &nativeError,
                                       message,
                                       sizeof(message),
                                       &length))) {
        return "";
      }
      return std::string(reinterpret_cast<char*>(state)) + " " +
             std::string(reinterpret_cast<char*>(message));
    }

    // Runs a query; returns the value of its single cell, or the SQLSTATE
    // and message prefixed with "ERROR " if it fails.
    std::string query() {
      SQLHSTMT hStmt = nullptr;
      EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt), SQL_SUCCESS);
      std::string result;
      SQLRETURN ret = SQLExecDirect(hStmt, (SQLCHAR*)"SELECT token", SQL_NTS);
      if (SQL_SUCCEEDED(ret)) {
        ret = SQLFetch(hStmt);
      }
      if (SQL_SUCCEEDED(ret)) {
        char buffer[128] = {0};
        SQLLEN indicator = 0;
        ret              = SQLGetData(
            hStmt, 1, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
        result = buffer;
      }
      if (!SQL_SUCCEEDED(ret)) {
        result = "ERROR " + diag(SQL_HANDLE_STMT, hStmt);
      }
      SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
      return result;
    }
};

TEST_F(TokenRenewalTest, OpaqueTokenIsUsedUntilRejected) {
  ASSERT_NO_FATAL_FAILURE(connect());
  EXPECT_EQ(query(), "token-1");
  EXPECT_EQ(query(), "token-1");
  // Its expiry can't be read: it's kept, not renewed on every request.
  EXPECT_EQ(stat("issued"), 1);
}

TEST_F(TokenRenewalTest, RejectedTokenIsRenewedOnPost) {
  ASSERT_NO_FATAL_FAILURE(connect());
  EXPECT_EQ(query(), "token-1");
  control("POST", "/control/revoke");
  // HTTP 401 with a token server only: a new token, no browser sign-in.
  EXPECT_EQ(query(), "token-2");
  EXPECT_EQ(query(), "token-2");
  EXPECT_EQ(stat("issued"), 2);
}

TEST_F(TokenRenewalTest, RejectedTokenIsRenewedWhileReadingResults) {
  ASSERT_NO_FATAL_FAILURE(connect());
  EXPECT_EQ(query(), "token-1");
  // The token is revoked between posting the query and reading its pages.
  control("POST", "/control/revoke-after-post");
  EXPECT_EQ(query(), "token-2");
  EXPECT_EQ(stat("issued"), 2);
}

TEST_F(TokenRenewalTest, RenewsOnlyOncePerRequest) {
  ASSERT_NO_FATAL_FAILURE(connect());
  EXPECT_EQ(query(), "token-1");
  control("POST", "/control/reject-all");
  const std::string result = query();
  EXPECT_EQ(result.rfind("ERROR 28000", 0), 0u) << result;
  // One renewal, then the error: no login loop.
  EXPECT_EQ(stat("issued"), 2);
}

TEST_F(TokenRenewalTest, TokenLifetimeRenewsAhead) {
  ASSERT_NO_FATAL_FAILURE(connect("tokenLifetime=1;"));
  EXPECT_EQ(query(), "token-1");
  std::this_thread::sleep_for(std::chrono::milliseconds(2100));
  const int rejectedBefore = stat("rejected");
  EXPECT_EQ(query(), "token-2");
  // Renewed before the server had to reject it.
  EXPECT_EQ(stat("rejected"), rejectedBefore + 1)
      << "only the anonymous challenge request of the new login";
}
