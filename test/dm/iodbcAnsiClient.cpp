/*
End-to-end check of a narrow-encoding ANSI application through the iODBC
driver manager, the way Excel for Mac (Microsoft Query) uses it: narrow
(A) calls only, SQL_C_CHAR data, text in a legacy Mac encoding (here Mac
Cyrillic, what Excel uses with a Russian UI), the "C" locale.

iODBC turns these A calls into the driver's W functions by widening every
byte; the driver must read them back in the connection's ansiEncoding.

Usage: IodbcAnsiClient [connection string]   (default "DSN=TrinoNoAuth")
"ansiEncoding=x-mac-cyrillic" is appended. Needs ODBCINI / ODBCINSTINI
pointing at a configuration that registers the driver (see the README) and
a running Trino server.

Links only libiodbc, never the driver itself.
*/

#include <sql.h>
#include <sqlext.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

// Bytes as hex, so the output doesn't depend on the terminal's encoding.
std::string hex(const std::string& bytes) {
  std::string out;
  for (const char c : bytes) {
    char part[4];
    static_cast<void>(std::snprintf(
        part, sizeof(part), "%02X ", static_cast<unsigned char>(c)));
    out += part;
  }
  return out;
}

void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

void checkEqual(const std::string& actual,
                const std::string& expected,
                const char* what) {
  check(actual == expected, what);
  if (actual != expected) {
    std::printf("  expected: %s\n  actual:   %s\n",
                hex(expected).c_str(),
                hex(actual).c_str());
  }
}

std::string diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
  SQLCHAR state[6]     = {};
  SQLCHAR message[512] = {};
  SQLINTEGER native    = 0;
  SQLSMALLINT length   = 0;
  if (SQL_SUCCEEDED(SQLGetDiagRec(type,
                                  handle,
                                  1,
                                  state,
                                  &native,
                                  message,
                                  sizeof(message),
                                  &length))) {
    return std::string(reinterpret_cast<char*>(state)) + " " +
           reinterpret_cast<char*>(message);
  }
  return "(no diagnostics)";
}

bool succeeded(SQLRETURN rc,
               SQLSMALLINT type,
               SQLHANDLE handle,
               const char* what) {
  const bool ok = SQL_SUCCEEDED(rc);
  check(ok, what);
  if (!ok) {
    std::printf("  %s\n", diagnostics(type, handle).c_str());
  }
  return ok;
}

std::string getCharData(SQLHSTMT stmt, SQLUSMALLINT column) {
  char buffer[256] = {};
  SQLLEN indicator = 0;
  const SQLRETURN rc =
      SQLGetData(stmt, column, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
  if (!SQL_SUCCEEDED(rc) || indicator < 0) {
    return {"(no data)"};
  }
  return std::string(buffer, std::strlen(buffer));
}

// Mac Cyrillic (x-mac-cyrillic) bytes.
const std::string PRIVET = "\x8F\xF0\xE8\xE2\xE5\xF2"; // Привет
const std::string MOSKVA = "\x8C\xEE\xF1\xEA\xE2\xE0"; // Москва
const std::string IMYA   = "\xE8\xEC\xDF";             // имя

} // namespace

int main(int argc, char** argv) {
  // Like a Cocoa application: the "C" locale, never setlocale(LC_ALL, "").
  std::string connStr = argc > 1 ? argv[1] : "DSN=TrinoNoAuth";
  // A non-ASCII value the driver doesn't use: it must come back unchanged
  // in the completed connection string (Excel stores that one).
  connStr += ";ansiEncoding=x-mac-cyrillic;Description=" + IMYA;

  SQLHENV env   = SQL_NULL_HENV;
  SQLHDBC dbc   = SQL_NULL_HDBC;
  SQLHSTMT stmt = SQL_NULL_HSTMT;
  SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
  SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);

  SQLCHAR completed[1024]     = {};
  SQLSMALLINT completedLength = 0;
  if (!succeeded(SQLDriverConnect(dbc,
                                  nullptr,
                                  (SQLCHAR*)connStr.c_str(),
                                  SQL_NTS,
                                  completed,
                                  sizeof(completed),
                                  &completedLength,
                                  SQL_DRIVER_NOPROMPT),
                 SQL_HANDLE_DBC,
                 dbc,
                 "SQLDriverConnect")) {
    return 1;
  }
  checkEqual(reinterpret_cast<char*>(completed),
             connStr,
             "completed connection string");
  SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

  // Text Trino generates itself (not an echo of the input): an error
  // message quoting chr(1055), П, must arrive in Mac Cyrillic (0x8F).
  {
    const std::string bad = "SELECT CAST(chr(1055) AS integer)";
    // Trino may report the failure on the first fetch.
    SQLRETURN rc = SQLExecDirect(stmt, (SQLCHAR*)bad.c_str(), SQL_NTS);
    if (SQL_SUCCEEDED(rc)) {
      rc = SQLFetch(stmt);
    }
    check(rc == SQL_ERROR, "invalid cast fails");
    const std::string message = diagnostics(SQL_HANDLE_STMT, stmt);
    check(message.find('\x8F') != std::string::npos,
          "server error message in Mac Cyrillic");
    if (message.find('\x8F') == std::string::npos) {
      std::printf("  message: %s\n", hex(message.substr(0, 120)).c_str());
    }
    SQLFreeStmt(stmt, SQL_CLOSE);
  }

  // A Cyrillic literal, a Cyrillic filter and a Cyrillic column alias, all
  // in the application's encoding. codepoint() shows what Trino received:
  // U+041F for П, not U+008F (the widened byte).
  const std::string query =
      "SELECT x AS \"" + IMYA + "\", '" + PRIVET +
      "' AS greeting, CAST(codepoint(CAST(substr('" + PRIVET +
      "', 1, 1) AS varchar(1))) AS varchar) AS first "
      "FROM (VALUES '" +
      PRIVET + "', '" + MOSKVA + "') AS t(x) WHERE x = '" + MOSKVA + "'";
  if (succeeded(SQLExecDirect(stmt, (SQLCHAR*)query.c_str(), SQL_NTS),
                SQL_HANDLE_STMT,
                stmt,
                "SQLExecDirect with Cyrillic text")) {
    SQLCHAR name[64]     = {};
    SQLSMALLINT nameLen  = 0;
    SQLSMALLINT type     = 0;
    SQLULEN size         = 0;
    SQLSMALLINT digits   = 0;
    SQLSMALLINT nullable = 0;
    if (succeeded(SQLDescribeCol(stmt,
                                 1,
                                 name,
                                 sizeof(name),
                                 &nameLen,
                                 &type,
                                 &size,
                                 &digits,
                                 &nullable),
                  SQL_HANDLE_STMT,
                  stmt,
                  "SQLDescribeCol")) {
      checkEqual(reinterpret_cast<char*>(name), IMYA, "column alias");
    }

    if (succeeded(SQLFetch(stmt), SQL_HANDLE_STMT, stmt, "SQLFetch")) {
      checkEqual(getCharData(stmt, 1), MOSKVA, "filtered row (WHERE)");
      checkEqual(getCharData(stmt, 2), PRIVET, "literal");
      checkEqual(getCharData(stmt, 3), "1055", "Trino got U+041F");
    }
    check(SQLFetch(stmt) == SQL_NO_DATA, "the filter keeps one row");
  }

  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);

  std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILED");
  return failures == 0 ? 0 : 1;
}
