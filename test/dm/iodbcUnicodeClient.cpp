/*
End-to-end check of the driver's Unicode API through the iODBC driver
manager, the way Excel for Mac uses it: a Unicode application (SQL*W calls,
SQL_C_WCHAR data) in the "C" locale.

Without the driver's W functions iODBC converts strings itself with the
process locale, and non-ASCII text comes back mangled; this program fails
then.

Usage: IodbcUnicodeClient [connection string]   (default "DSN=TrinoNoAuth")
Needs ODBCINI / ODBCINSTINI pointing at a configuration that registers the
driver (see the README) and a running Trino server.

Links only libiodbc, never the driver itself.
*/

#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

static_assert(sizeof(SQLWCHAR) == sizeof(wchar_t),
              "iODBC on macOS uses wchar_t for SQLWCHAR");

namespace {

int failures = 0;

std::string narrow(const std::wstring& text) {
  // Code points as escapes, so the output doesn't depend on the locale.
  std::string out;
  for (wchar_t c : text) {
    if (c >= 0x20 && c < 0x7F) {
      out.push_back(static_cast<char>(c));
    } else {
      char escape[16];
      std::snprintf(
          escape, sizeof(escape), "\\u%04X", static_cast<unsigned>(c));
      out += escape;
    }
  }
  return out;
}

void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

void checkEqual(const std::wstring& actual,
                const std::wstring& expected,
                const char* what) {
  const bool ok = actual == expected;
  check(ok, what);
  if (!ok) {
    std::printf("  expected \"%s\"\n  actual   \"%s\"\n",
                narrow(expected).c_str(),
                narrow(actual).c_str());
  }
}

std::wstring diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
  SQLWCHAR state[6]      = {};
  SQLWCHAR message[2048] = {};
  SQLINTEGER native      = 0;
  SQLSMALLINT length     = 0;
  if (!SQL_SUCCEEDED(SQLGetDiagRecW(
          type, handle, 1, state, &native, message, 2048, &length))) {
    return L"<no diagnostics>";
  }
  return std::wstring(state) + L" " + message;
}

bool succeeded(SQLRETURN rc,
               SQLSMALLINT type,
               SQLHANDLE handle,
               const char* what) {
  const bool ok = SQL_SUCCEEDED(rc);
  check(ok, what);
  if (!ok) {
    std::printf("  rc=%d %s\n", rc, narrow(diagnostics(type, handle)).c_str());
  }
  return ok;
}

std::wstring
getWideData(SQLHSTMT stmt, SQLUSMALLINT column, SQLLEN* indicator) {
  std::vector<SQLWCHAR> buffer(4096, 0);
  SQLLEN length = 0;
  SQLRETURN rc =
      SQLGetData(stmt,
                 column,
                 SQL_C_WCHAR,
                 buffer.data(),
                 static_cast<SQLLEN>(buffer.size() * sizeof(SQLWCHAR)),
                 &length);
  if (indicator) {
    *indicator = length;
  }
  if (!SQL_SUCCEEDED(rc)) {
    return L"<SQLGetData failed: " + diagnostics(SQL_HANDLE_STMT, stmt) + L">";
  }
  return std::wstring(buffer.data());
}

std::wstring widen(const char* ascii) {
  std::wstring out;
  for (const char* p = ascii; *p; p++) {
    out.push_back(static_cast<wchar_t>(*p));
  }
  return out;
}

} // namespace

int main(int argc, char** argv) {
  // Deliberately no setlocale(): like a Cocoa app, stay in the "C" locale.
  const std::wstring connectionString =
      widen(argc > 1 ? argv[1] : "DSN=TrinoNoAuth");

  SQLHENV env   = SQL_NULL_HENV;
  SQLHDBC dbc   = SQL_NULL_HDBC;
  SQLHSTMT stmt = SQL_NULL_HSTMT;
  SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
  SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);

  std::vector<SQLWCHAR> in(connectionString.begin(), connectionString.end());
  in.push_back(0);
  SQLWCHAR out[1024]    = {};
  SQLSMALLINT outLength = 0;
  if (!succeeded(SQLDriverConnectW(dbc,
                                   nullptr,
                                   in.data(),
                                   SQL_NTS,
                                   out,
                                   1024,
                                   &outLength,
                                   SQL_DRIVER_NOPROMPT),
                 SQL_HANDLE_DBC,
                 dbc,
                 "SQLDriverConnectW")) {
    return 1;
  }

  SQLWCHAR dbms[64]      = {};
  SQLSMALLINT dbmsLength = 0;
  SQLGetInfoW(dbc, SQL_DBMS_NAME, dbms, sizeof(dbms), &dbmsLength);
  checkEqual(dbms, L"Trino", "SQLGetInfoW(SQL_DBMS_NAME)");

  SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);

  // A Cyrillic literal, a Cyrillic alias and a Unicode escape that Trino
  // decodes itself.
  const std::wstring query =
      L"SELECT 'Привет, мир' AS \"колонка\", "
      L"U&'\\041F\\0440\\0438\\0432\\0435\\0442' AS escaped, "
      L"'Кириллица текст' AS t";
  std::vector<SQLWCHAR> queryText(query.begin(), query.end());
  queryText.push_back(0);
  if (succeeded(SQLExecDirectW(stmt, queryText.data(), SQL_NTS),
                SQL_HANDLE_STMT,
                stmt,
                "SQLExecDirectW with Cyrillic text")) {
    SQLWCHAR name[64]      = {};
    SQLSMALLINT nameLength = 0;
    SQLDescribeColW(
        stmt, 1, name, 64, &nameLength, nullptr, nullptr, nullptr, nullptr);
    checkEqual(name, L"колонка", "SQLDescribeColW Cyrillic alias");
    check(nameLength == 7, "SQLDescribeColW length in characters");

    if (succeeded(SQLFetch(stmt), SQL_HANDLE_STMT, stmt, "SQLFetch")) {
      SQLLEN indicator = 0;
      checkEqual(getWideData(stmt, 1, &indicator),
                 L"Привет, мир",
                 "SQLGetData SQL_C_WCHAR literal");
      check(indicator == static_cast<SQLLEN>(11 * sizeof(SQLWCHAR)),
            "SQLGetData indicator in bytes");
      checkEqual(getWideData(stmt, 2, nullptr),
                 L"Привет",
                 "SQLGetData SQL_C_WCHAR U&'' escape");
      checkEqual(getWideData(stmt, 3, &indicator),
                 L"Кириллица текст",
                 "SQLGetData SQL_C_WCHAR 'Кириллица текст'");
      check(indicator == static_cast<SQLLEN>(15 * sizeof(SQLWCHAR)),
            "'Кириллица текст' is 15 characters");
    }
  }
  SQLFreeStmt(stmt, SQL_CLOSE);

  // Catalog functions with wide arguments.
  SQLWCHAR catalog[] = L"tpch";
  SQLWCHAR schema[]  = L"tiny";
  SQLWCHAR table[]   = L"nation";
  if (succeeded(SQLTablesW(stmt,
                           catalog,
                           SQL_NTS,
                           schema,
                           SQL_NTS,
                           table,
                           SQL_NTS,
                           nullptr,
                           0),
                SQL_HANDLE_STMT,
                stmt,
                "SQLTablesW") &&
      succeeded(SQLFetch(stmt), SQL_HANDLE_STMT, stmt, "SQLFetch tables")) {
    checkEqual(getWideData(stmt, 3, nullptr), L"nation", "SQLTablesW row");
  }
  SQLFreeStmt(stmt, SQL_CLOSE);

  if (succeeded(SQLColumnsW(stmt,
                            catalog,
                            SQL_NTS,
                            schema,
                            SQL_NTS,
                            table,
                            SQL_NTS,
                            nullptr,
                            0),
                SQL_HANDLE_STMT,
                stmt,
                "SQLColumnsW")) {
    int rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(stmt))) {
      rows++;
    }
    check(rows == 4, "SQLColumnsW returns the 4 nation columns");
  }
  SQLFreeStmt(stmt, SQL_CLOSE);

  // An error message that quotes Cyrillic text from the query.
  const std::wstring badQuery = L"SELECT * FROM tpch.tiny.\"нет_таблицы\"";
  std::vector<SQLWCHAR> badText(badQuery.begin(), badQuery.end());
  badText.push_back(0);
  // Trino reports the error with the first response or the first fetch.
  SQLRETURN rc = SQLExecDirectW(stmt, badText.data(), SQL_NTS);
  if (SQL_SUCCEEDED(rc)) {
    rc = SQLFetch(stmt);
  }
  check(rc == SQL_ERROR, "a query on a missing table fails");
  const std::wstring message = diagnostics(SQL_HANDLE_STMT, stmt);
  const bool quoted = message.find(L"нет_таблицы") != std::wstring::npos;
  check(quoted, "SQLGetDiagRecW message keeps Cyrillic text");
  if (!quoted) {
    std::printf("  message: %s\n", narrow(message).c_str());
  }

  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);

  std::printf("%s (%d failure%s)\n",
              failures == 0 ? "OK" : "FAILED",
              failures,
              failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
