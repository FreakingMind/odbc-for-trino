#include "../../src/util/windowsLean.hpp"

#include <gtest/gtest.h>
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <cstring>
#include <string>
#include <vector>

#include "../../src/driver/unicode/wideText.hpp"
#include "../constants.hpp"

/*
Integration tests for the ODBC Unicode (W) API: the functions Excel for Mac
calls through iODBC. They need the Trino server from the README.

Strings are written as UTF-8 and converted with the driver's helpers, so
the tests hold for 2-byte and 4-byte SQLWCHAR alike.
*/

namespace {

// "Привет, мир": 11 characters.
const std::string GREETING = "Привет, мир";
// "колонка": 7 characters.
const std::string ALIAS = "колонка";

std::vector<SQLWCHAR> W(const std::string& utf8) {
  const SqlWideString wide = utf8ToSqlWide(utf8);
  std::vector<SQLWCHAR> result(wide.size() + 1, 0);
  std::memcpy(result.data(), wide.data(), wide.size() * sizeof(SQLWCHAR));
  return result;
}

std::string U8(const SQLWCHAR* text) {
  return sqlWideToUtf8(text, SQL_NTS);
}

std::string U8(const std::vector<SQLWCHAR>& text) {
  return U8(text.data());
}

class UnicodeApiTest : public ::testing::Test {
  protected:
    SQLHENV hEnv   = nullptr;
    SQLHDBC hDbc   = nullptr;
    SQLHSTMT hStmt = nullptr;

    std::string connectionString() const {
      return "DSN=" + TEST_DSN + ";";
    }

    void SetUp() override {
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc), SQL_SUCCESS);

      std::vector<SQLWCHAR> in = W(connectionString());
      std::vector<SQLWCHAR> out(1024, 0);
      SQLSMALLINT outLength = 0;
      SQLRETURN rc = SQLDriverConnectW(hDbc,
                                       nullptr,
                                       in.data(),
                                       SQL_NTS,
                                       out.data(),
                                       static_cast<SQLSMALLINT>(out.size()),
                                       &outLength,
                                       SQL_DRIVER_NOPROMPT);
      ASSERT_EQ(rc, SQL_SUCCESS) << diag(SQL_HANDLE_DBC, hDbc);
      EXPECT_EQ(U8(out), connectionString());
      EXPECT_EQ(outLength, static_cast<SQLSMALLINT>(connectionString().size()));

      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt), SQL_SUCCESS);
    }

    void TearDown() override {
      if (hStmt) {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
      }
      if (hDbc) {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
      }
      if (hEnv) {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
      }
    }

    // First diagnostic record, read through SQLGetDiagRecW.
    std::string
    diag(SQLSMALLINT type, SQLHANDLE handle, std::string* state = nullptr) {
      SQLWCHAR sqlState[6]   = {};
      SQLWCHAR message[2048] = {};
      SQLINTEGER native      = 0;
      SQLSMALLINT length     = 0;
      SQLRETURN rc           = SQLGetDiagRecW(
          type, handle, 1, sqlState, &native, message, 2048, &length);
      if (!SQL_SUCCEEDED(rc)) {
        return "<no diagnostics>";
      }
      if (state) {
        *state = U8(sqlState);
      }
      return U8(message);
    }

    // A query on a missing table with a Cyrillic name. Trino reports the
    // error with the first response or the first fetch.
    void runMissingTableQuery() {
      std::vector<SQLWCHAR> text = W("SELECT * FROM tpch.tiny.\"нет_таблицы\"");
      SQLRETURN rc               = SQLExecDirectW(hStmt, text.data(), SQL_NTS);
      if (SQL_SUCCEEDED(rc)) {
        rc = SQLFetch(hStmt);
      }
      ASSERT_EQ(rc, SQL_ERROR);
    }

    void execDirectW(const std::string& query) {
      std::vector<SQLWCHAR> text = W(query);
      SQLRETURN rc               = SQLExecDirectW(hStmt, text.data(), SQL_NTS);
      ASSERT_EQ(rc, SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
    }

    // Column `column` of the current row as SQL_C_WCHAR.
    std::string getWideData(SQLUSMALLINT column, SQLLEN* indicator = nullptr) {
      std::vector<SQLWCHAR> buffer(4096, 0);
      SQLLEN length = 0;
      SQLRETURN rc =
          SQLGetData(hStmt,
                     column,
                     SQL_C_WCHAR,
                     buffer.data(),
                     static_cast<SQLLEN>(buffer.size() * sizeof(SQLWCHAR)),
                     &length);
      EXPECT_EQ(rc, SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
      if (indicator) {
        *indicator = length;
      }
      return U8(buffer);
    }
};

} // namespace

TEST_F(UnicodeApiTest, DriverConnectWReportsLengthWhenOutputIsTruncated) {
  SQLHDBC second = nullptr;
  ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &second), SQL_SUCCESS);
  std::vector<SQLWCHAR> in = W(connectionString());
  SQLWCHAR out[5]          = {};
  SQLSMALLINT outLength    = 0;
  SQLRETURN rc             = SQLDriverConnectW(second,
                                               nullptr,
                                               in.data(),
                                               SQL_NTS,
                                               out,
                                               5,
                                               &outLength,
                                               SQL_DRIVER_NOPROMPT);
  EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(U8(out), connectionString().substr(0, 4));
  EXPECT_EQ(outLength, static_cast<SQLSMALLINT>(connectionString().size()));
  std::string state;
  diag(SQL_HANDLE_DBC, second, &state);
  EXPECT_EQ(state, "01004");
  SQLDisconnect(second);
  SQLFreeHandle(SQL_HANDLE_DBC, second);
}

TEST_F(UnicodeApiTest, ExecDirectWRoundTripsCyrillic) {
  execDirectW("SELECT '" + GREETING + "' AS \"" + ALIAS +
              "\", U&'\\041F\\0440\\0438\\0432\\0435\\0442' AS escaped, "
              "'\xF0\x9F\x98\x80' AS emoji");

  std::vector<SQLWCHAR> name(64, 0);
  SQLSMALLINT nameLength = 0;
  SQLSMALLINT dataType   = 0;
  SQLULEN columnSize     = 0;
  ASSERT_EQ(SQLDescribeColW(hStmt,
                            1,
                            name.data(),
                            static_cast<SQLSMALLINT>(name.size()),
                            &nameLength,
                            &dataType,
                            &columnSize,
                            nullptr,
                            nullptr),
            SQL_SUCCESS);
  EXPECT_EQ(U8(name), ALIAS);
  EXPECT_EQ(nameLength, 7); // Characters, not bytes.
  EXPECT_EQ(columnSize, 11u);

  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
  SQLLEN indicator = 0;
  EXPECT_EQ(getWideData(1, &indicator), GREETING);
  EXPECT_EQ(indicator, static_cast<SQLLEN>(11 * sizeof(SQLWCHAR)));
  EXPECT_EQ(getWideData(2), "Привет");
  EXPECT_EQ(getWideData(3, &indicator), "\xF0\x9F\x98\x80");
  // One UTF-32 unit or a UTF-16 surrogate pair: 4 bytes either way.
  EXPECT_EQ(indicator, 4);
}

TEST_F(UnicodeApiTest, PrepareWAndExecute) {
  std::vector<SQLWCHAR> text = W("SELECT 'Кириллица текст' AS t");
  ASSERT_EQ(SQLPrepareW(hStmt, text.data(), SQL_NTS), SQL_SUCCESS)
      << diag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(SQLExecute(hStmt), SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
  EXPECT_EQ(getWideData(1), "Кириллица текст");
}

TEST_F(UnicodeApiTest, ExecDirectWHonoursExplicitLength) {
  // The length is in characters; the rest of the buffer is ignored.
  std::vector<SQLWCHAR> text = W("SELECT 'ё' AS x; garbage");
  ASSERT_EQ(SQLExecDirectW(hStmt, text.data(), 15), SQL_SUCCESS)
      << diag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(getWideData(1), "ё");
}

TEST_F(UnicodeApiTest, DescribeColWTruncatesInCharacters) {
  execDirectW("SELECT 1 AS \"" + ALIAS + "\"");
  SQLWCHAR name[4]       = {};
  SQLSMALLINT nameLength = 0;
  SQLRETURN rc           = SQLDescribeColW(
      hStmt, 1, name, 4, &nameLength, nullptr, nullptr, nullptr, nullptr);
  EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(U8(name), "кол");
  EXPECT_EQ(nameLength, 7);
  std::string state;
  diag(SQL_HANDLE_STMT, hStmt, &state);
  EXPECT_EQ(state, "01004");
}

TEST_F(UnicodeApiTest, ColAttributeWCountsBytes) {
  execDirectW("SELECT 1 AS \"" + ALIAS + "\"");
  std::vector<SQLWCHAR> name(64, 0);
  SQLSMALLINT length = 0;
  ASSERT_EQ(
      SQLColAttributeW(hStmt,
                       1,
                       SQL_DESC_NAME,
                       name.data(),
                       static_cast<SQLSMALLINT>(name.size() * sizeof(SQLWCHAR)),
                       &length,
                       nullptr),
      SQL_SUCCESS);
  EXPECT_EQ(U8(name), ALIAS);
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(7 * sizeof(SQLWCHAR)));

  // Room for 3 characters plus the terminator.
  SQLWCHAR small[4] = {};
  EXPECT_EQ(
      SQLColAttributeW(
          hStmt, 1, SQL_DESC_NAME, small, sizeof(small), &length, nullptr),
      SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(U8(small), "кол");
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(7 * sizeof(SQLWCHAR)));

  // Numeric attributes pass through.
  SQLLEN type = 0;
  EXPECT_EQ(SQLColAttributeW(
                hStmt, 1, SQL_DESC_CONCISE_TYPE, nullptr, 0, nullptr, &type),
            SQL_SUCCESS);
  EXPECT_EQ(type, SQL_INTEGER);
}

TEST_F(UnicodeApiTest, TablesWFindsNation) {
  std::vector<SQLWCHAR> catalog = W("tpch");
  std::vector<SQLWCHAR> schema  = W("tiny");
  std::vector<SQLWCHAR> table   = W("nation");
  ASSERT_EQ(SQLTablesW(hStmt,
                       catalog.data(),
                       SQL_NTS,
                       schema.data(),
                       SQL_NTS,
                       table.data(),
                       SQL_NTS,
                       nullptr,
                       0),
            SQL_SUCCESS)
      << diag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS) << diag(SQL_HANDLE_STMT, hStmt);
  EXPECT_EQ(getWideData(1), "tpch");
  EXPECT_EQ(getWideData(2), "tiny");
  EXPECT_EQ(getWideData(3), "nation");
  EXPECT_EQ(SQLFetch(hStmt), SQL_NO_DATA);
}

TEST_F(UnicodeApiTest, ColumnsWListsNationColumns) {
  std::vector<SQLWCHAR> catalog = W("tpch");
  std::vector<SQLWCHAR> schema  = W("tiny");
  std::vector<SQLWCHAR> table   = W("nation");
  ASSERT_EQ(SQLColumnsW(hStmt,
                        catalog.data(),
                        SQL_NTS,
                        schema.data(),
                        SQL_NTS,
                        table.data(),
                        SQL_NTS,
                        nullptr,
                        0),
            SQL_SUCCESS)
      << diag(SQL_HANDLE_STMT, hStmt);
  std::vector<std::string> columns;
  while (SQLFetch(hStmt) == SQL_SUCCESS) {
    columns.push_back(getWideData(4));
  }
  EXPECT_EQ(
      columns,
      (std::vector<std::string>{"nationkey", "name", "regionkey", "comment"}));
}

TEST_F(UnicodeApiTest, GetInfoWReturnsDriverAndDbmsNames) {
  std::vector<SQLWCHAR> value(64, 0);
  const auto bytes = static_cast<SQLSMALLINT>(value.size() * sizeof(SQLWCHAR));
  SQLSMALLINT length = 0;
  ASSERT_EQ(SQLGetInfoW(hDbc, SQL_DRIVER_NAME, value.data(), bytes, &length),
            SQL_SUCCESS);
  EXPECT_EQ(U8(value), "TrinoODBC");
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(9 * sizeof(SQLWCHAR)));
  ASSERT_EQ(SQLGetInfoW(hDbc, SQL_DBMS_NAME, value.data(), bytes, &length),
            SQL_SUCCESS);
  EXPECT_EQ(U8(value), "Trino");
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(5 * sizeof(SQLWCHAR)));
}

TEST_F(UnicodeApiTest, GetConnectAttrWCurrentCatalog) {
  std::vector<SQLWCHAR> value(64, 0);
  SQLINTEGER length = 0;
  ASSERT_EQ(SQLGetConnectAttrW(
                hDbc,
                SQL_ATTR_CURRENT_CATALOG,
                value.data(),
                static_cast<SQLINTEGER>(value.size() * sizeof(SQLWCHAR)),
                &length),
            SQL_SUCCESS);
  EXPECT_EQ(U8(value), "system");
  EXPECT_EQ(length, static_cast<SQLINTEGER>(6 * sizeof(SQLWCHAR)));
}

TEST_F(UnicodeApiTest, GetDiagRecWReturnsCyrillicServerError) {
  runMissingTableQuery();
  std::string state;
  const std::string message = diag(SQL_HANDLE_STMT, hStmt, &state);
  EXPECT_EQ(state.size(), 5u);
  EXPECT_NE(message.find("нет_таблицы"), std::string::npos) << message;
}

TEST_F(UnicodeApiTest, GetDiagRecWSplitsLongErrorsIntoRecordsThatFit) {
  runMissingTableQuery();
  // Every record fits the caller's buffer (counted in characters).
  constexpr SQLSMALLINT BUFFER_CHARS = 40;
  std::string all;
  SQLSMALLINT record = 1;
  for (;; record++) {
    SQLWCHAR message[BUFFER_CHARS] = {};
    SQLSMALLINT length             = 0;
    SQLRETURN rc                   = SQLGetDiagRecW(SQL_HANDLE_STMT,
                                                    hStmt,
                                                    record,
                                                    nullptr,
                                                    nullptr,
                                                    message,
                                                    BUFFER_CHARS,
                                                    &length);
    if (rc == SQL_NO_DATA) {
      break;
    }
    ASSERT_EQ(rc, SQL_SUCCESS) << "record " << record;
    EXPECT_LT(length, BUFFER_CHARS);
    all += U8(message);
    ASSERT_LT(record, 1000);
  }
  EXPECT_GT(record, 2);
  // Each record starts and ends with a newline; a line that doesn't fit is
  // continued in the next record.
  std::erase(all, '\n');
  EXPECT_NE(all.find("нет_таблицы"), std::string::npos) << all;
}

TEST_F(UnicodeApiTest, GetTypeInfoWAndStmtAttrsPassThrough) {
  ASSERT_EQ(SQLGetTypeInfoW(hStmt, SQL_VARCHAR), SQL_SUCCESS)
      << diag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(getWideData(1), "VARCHAR");
  SQLFreeStmt(hStmt, SQL_CLOSE);

  ASSERT_EQ(SQLSetStmtAttrW(hStmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)5, 0),
            SQL_SUCCESS);
  SQLULEN maxRows = 0;
  ASSERT_EQ(SQLGetStmtAttrW(hStmt, SQL_ATTR_MAX_ROWS, &maxRows, 0, nullptr),
            SQL_SUCCESS);
  EXPECT_EQ(maxRows, 5u);
}
