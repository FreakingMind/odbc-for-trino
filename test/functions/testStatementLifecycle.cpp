#include "../../src/util/windowsLean.hpp"

#include <sql.h>
#include <sqlext.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>

#include "../../src/driver/handles/statementHandle.hpp"
#include "../fixtures/sqlDriverConnectFixture.hpp"

/*
Statement life cycle (close/re-execute with bindings), diagnostics,
conversions of bound columns and buffer-length handling. These run against
the Trino of the test DSN.
*/

namespace {

struct Diag {
    SQLRETURN ret = SQL_ERROR;
    std::string state;
    std::string message;
    SQLSMALLINT textLength = 0;
};

Diag getDiag(SQLSMALLINT handleType,
             SQLHANDLE handle,
             SQLSMALLINT recNumber    = 1,
             SQLSMALLINT bufferLength = 1024) {
  Diag diag;
  SQLCHAR state[6]      = {};
  SQLCHAR message[1024] = {};
  SQLINTEGER native     = 0;
  diag.ret              = SQLGetDiagRec(handleType,
                                        handle,
                                        recNumber,
                                        state,
                                        &native,
                                        message,
                                        bufferLength,
                                        &diag.textLength);
  if (diag.ret == SQL_SUCCESS || diag.ret == SQL_SUCCESS_WITH_INFO) {
    diag.state   = reinterpret_cast<char*>(state);
    diag.message = reinterpret_cast<char*>(message);
  }
  return diag;
}

std::wstring toWide(const SQLWCHAR* text) {
  std::wstring result;
  for (; *text; ++text) {
    result.push_back(static_cast<wchar_t>(*text));
  }
  return result;
}

} // namespace

class StatementLifecycleTest : public SQLDriverConnectFixture {
  protected:
    void SetUp() override {
      SQLDriverConnectFixture::SetUp();
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt), SQL_SUCCESS);
    }

    void TearDown() override {
      if (hStmt) {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        hStmt = nullptr;
      }
      SQLDriverConnectFixture::TearDown();
    }

    void execDirect(const std::string& query) {
      SQLRETURN ret = SQLExecDirect(hStmt, (SQLCHAR*)query.c_str(), SQL_NTS);
      maybeReportStatementError(ret);
      ASSERT_EQ(ret, SQL_SUCCESS);
    }

    std::string diagState(SQLSMALLINT recNumber = 1) {
      return getDiag(SQL_HANDLE_STMT, hStmt, recNumber).state;
    }
};

// Scenario A: a prepared statement with a bound parameter must keep the
// parameter across SQLCloseCursor and produce the same kind of result.
TEST_F(StatementLifecycleTest, CloseCursorKeepsParameterBindings) {
  const std::string query = "SELECT CAST(? AS BIGINT) * 2";
  ASSERT_EQ(SQLPrepare(hStmt, (SQLCHAR*)query.c_str(), SQL_NTS), SQL_SUCCESS);

  SQLBIGINT param = 21;
  SQLLEN paramInd = 0;
  SQLRETURN ret   = SQLBindParameter(hStmt,
                                     1,
                                     SQL_PARAM_INPUT,
                                     SQL_C_SBIGINT,
                                     SQL_BIGINT,
                                     0,
                                     0,
                                     &param,
                                     0,
                                     &paramInd);
  ASSERT_EQ(ret, SQL_SUCCESS);

  SQLBIGINT result = 0;
  SQLLEN resultInd = 0;
  ASSERT_EQ(
      SQLBindCol(hStmt, 1, SQL_C_SBIGINT, &result, sizeof(result), &resultInd),
      SQL_SUCCESS);

  ret = SQLExecute(hStmt);
  maybeReportStatementError(ret);
  ASSERT_EQ(ret, SQL_SUCCESS);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(result, 42);

  ASSERT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);

  // Same bindings, new parameter value.
  param  = 5;
  result = 0;
  ret    = SQLExecute(hStmt);
  maybeReportStatementError(ret);
  ASSERT_EQ(ret, SQL_SUCCESS);
  ret = SQLFetch(hStmt);
  maybeReportStatementError(ret);
  ASSERT_EQ(ret, SQL_SUCCESS);
  EXPECT_EQ(result, 10);
  EXPECT_EQ(SQLFetch(hStmt), SQL_NO_DATA);

  // SQLFreeStmt(SQL_CLOSE) behaves the same.
  ASSERT_EQ(SQLFreeStmt(hStmt, SQL_CLOSE), SQL_SUCCESS);
  param = 50;
  ASSERT_EQ(SQLExecute(hStmt), SQL_SUCCESS);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(result, 100);
}

// Scenario B: bound columns must still be filled after close + re-execute.
TEST_F(StatementLifecycleTest, CloseCursorKeepsColumnBindings) {
  const std::string query =
      "SELECT x, CAST(x AS VARCHAR) FROM UNNEST(sequence(1, 3)) AS t(x)";

  SQLINTEGER number = 0;
  SQLLEN numberInd  = 0;
  char text[16]     = {};
  SQLLEN textInd    = 0;

  execDirect(query);
  ASSERT_EQ(
      SQLBindCol(hStmt, 1, SQL_C_SLONG, &number, sizeof(number), &numberInd),
      SQL_SUCCESS);
  ASSERT_EQ(SQLBindCol(hStmt, 2, SQL_C_CHAR, text, sizeof(text), &textInd),
            SQL_SUCCESS);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(number, 1);

  // Close in the middle of the result set.
  ASSERT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);

  number = -1;
  std::memset(text, 0, sizeof(text));
  execDirect(query);
  int rows = 0;
  while (SQLFetch(hStmt) == SQL_SUCCESS) {
    rows++;
    EXPECT_EQ(number, rows);
    EXPECT_EQ(std::string(text), std::to_string(rows));
    EXPECT_EQ(textInd, 1);
  }
  EXPECT_EQ(rows, 3);
}

TEST_F(StatementLifecycleTest, CloseCursorWithoutOpenCursor) {
  // Deliberately lenient (see closeCursor.cpp): iODBC passes the call
  // through and applications close defensively, so a close without an open
  // cursor is a successful no-op, like SQLFreeStmt(SQL_CLOSE).
  EXPECT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);
  EXPECT_EQ(SQLFreeStmt(hStmt, SQL_CLOSE), SQL_SUCCESS);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);

  execDirect("SELECT 1");
  EXPECT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);
  // Closing twice is harmless too.
  EXPECT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);
}

TEST_F(StatementLifecycleTest, CloseCursorCancelsRunningQuery) {
  execDirect("SELECT * FROM tpch.sf100.lineitem");
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);

  const std::string queryId =
      reinterpret_cast<Statement*>(hStmt)->trinoQuery->getQueryId();
  ASSERT_FALSE(queryId.empty());

  ASSERT_EQ(SQLCloseCursor(hStmt), SQL_SUCCESS);
  EXPECT_EQ(SQLGetData(hStmt, 1, SQL_C_CHAR, nullptr, 0, nullptr), SQL_ERROR);
  EXPECT_EQ(diagState(), "24000");

  // The connection still works after the cancellation, and Trino reports
  // the query as canceled.
  std::string state;
  for (int attempt = 0; attempt < 50 && state != "FAILED"; attempt++) {
    execDirect("SELECT state FROM system.runtime.queries WHERE query_id = '" +
               queryId + "'");
    char buffer[32] = {};
    SQLLEN ind      = 0;
    ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
    ASSERT_EQ(SQLGetData(hStmt, 1, SQL_C_CHAR, buffer, sizeof(buffer), &ind),
              SQL_SUCCESS);
    state = buffer;
    ASSERT_EQ(SQLFreeStmt(hStmt, SQL_CLOSE), SQL_SUCCESS);
    if (state != "FAILED") {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  EXPECT_EQ(state, "FAILED");
}

TEST_F(StatementLifecycleTest, FreeStmtUnbindAndResetParams) {
  SQLBIGINT result = -1;
  SQLLEN ind       = 0;
  ASSERT_EQ(SQLBindCol(hStmt, 1, SQL_C_SBIGINT, &result, sizeof(result), &ind),
            SQL_SUCCESS);
  ASSERT_EQ(SQLFreeStmt(hStmt, SQL_UNBIND), SQL_SUCCESS);
  execDirect("SELECT BIGINT '7'");
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  // The column isn't bound any more.
  EXPECT_EQ(result, -1);
  ASSERT_EQ(SQLGetData(hStmt, 1, SQL_C_SBIGINT, &result, 0, &ind), SQL_SUCCESS);
  EXPECT_EQ(result, 7);

  const std::string query = "SELECT CAST(? AS BIGINT)";
  ASSERT_EQ(SQLPrepare(hStmt, (SQLCHAR*)query.c_str(), SQL_NTS), SQL_SUCCESS);
  SQLBIGINT param = 3;
  ASSERT_EQ(SQLBindParameter(hStmt,
                             1,
                             SQL_PARAM_INPUT,
                             SQL_C_SBIGINT,
                             SQL_BIGINT,
                             0,
                             0,
                             &param,
                             0,
                             nullptr),
            SQL_SUCCESS);
  ASSERT_EQ(SQLFreeStmt(hStmt, SQL_RESET_PARAMS), SQL_SUCCESS);
  // Without the parameter, Trino rejects the EXECUTE.
  SQLRETURN ret = SQLExecute(hStmt);
  if (ret == SQL_SUCCESS) {
    SQLSMALLINT columns = 0;
    ret                 = SQLNumResultCols(hStmt, &columns);
  }
  EXPECT_EQ(ret, SQL_ERROR);
  EXPECT_FALSE(diagState().empty());
}

TEST_F(StatementLifecycleTest, GetDataWithoutCurrentRow) {
  execDirect("SELECT 1");
  SQLINTEGER value = 0;
  SQLLEN ind       = 0;
  // Before the first fetch.
  EXPECT_EQ(SQLGetData(hStmt, 1, SQL_C_SLONG, &value, 0, &ind), SQL_ERROR);
  EXPECT_EQ(diagState(), "24000");
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(SQLGetData(hStmt, 1, SQL_C_SLONG, &value, 0, &ind), SQL_SUCCESS);
  EXPECT_EQ(value, 1);
  // After the last row.
  ASSERT_EQ(SQLFetch(hStmt), SQL_NO_DATA);
  EXPECT_EQ(SQLGetData(hStmt, 1, SQL_C_SLONG, &value, 0, &ind), SQL_ERROR);
  EXPECT_EQ(diagState(), "24000");
}

TEST_F(StatementLifecycleTest, FetchBeforeExecuteIsSequenceError) {
  EXPECT_EQ(SQLFetch(hStmt), SQL_ERROR);
  EXPECT_EQ(diagState(), "HY010");
}

// ---------------------------------------------------------------------------
// Diagnostics are cleared by the next function call on the handle.
// ---------------------------------------------------------------------------
TEST_F(StatementLifecycleTest, DriverDiagnosticsAreCleared) {
  SQLULEN value = 0;
  EXPECT_EQ(SQLGetStmtAttr(hStmt, 123456, &value, 0, nullptr), SQL_ERROR);
  EXPECT_EQ(diagState(), "HY092");
  // Asking twice doesn't clear the record.
  EXPECT_EQ(diagState(), "HY092");

  execDirect("SELECT 1");
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);

  EXPECT_EQ(SQLSetStmtAttr(hStmt, 123456, nullptr, 0), SQL_ERROR);
  EXPECT_EQ(diagState(), "HY092");
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);
}

TEST_F(StatementLifecycleTest, ServerDiagnosticsAreCleared) {
  const std::string bad = "SELECT * FROM tpch.tiny.no_such_table";
  SQLRETURN ret         = SQLExecDirect(hStmt, (SQLCHAR*)bad.c_str(), SQL_NTS);
  if (ret == SQL_SUCCESS) {
    SQLSMALLINT columns = 0;
    ret                 = SQLNumResultCols(hStmt, &columns);
  }
  ASSERT_EQ(ret, SQL_ERROR);
  Diag diag = getDiag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(diag.ret, SQL_SUCCESS);
  EXPECT_FALSE(diag.state.empty());
  EXPECT_NE(diag.message.find("no_such_table"), std::string::npos)
      << diag.message;

  // An unrelated successful call clears the server error, too.
  SQLULEN rowArraySize = 0;
  ASSERT_EQ(
      SQLGetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, &rowArraySize, 0, nullptr),
      SQL_SUCCESS);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);

  // And so does a new, successful query.
  ret = SQLExecDirect(hStmt, (SQLCHAR*)bad.c_str(), SQL_NTS);
  if (ret == SQL_SUCCESS) {
    SQLSMALLINT columns = 0;
    ret                 = SQLNumResultCols(hStmt, &columns);
  }
  ASSERT_EQ(ret, SQL_ERROR);
  execDirect("SELECT 1");
  SQLSMALLINT columns = 0;
  ASSERT_EQ(SQLNumResultCols(hStmt, &columns), SQL_SUCCESS);
  EXPECT_EQ(columns, 1);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt).ret, SQL_NO_DATA);
}

TEST_F(StatementLifecycleTest, FailingQueryIsReportedByFetch) {
  // The error happens while the rows are produced, after the columns are
  // known: SQLFetch must not report it as the end of the data.
  execDirect("SELECT 1 / (x - 3) FROM UNNEST(sequence(1, 5)) AS t(x)");
  SQLRETURN ret = SQL_SUCCESS;
  int rows      = 0;
  while ((ret = SQLFetch(hStmt)) == SQL_SUCCESS) {
    rows++;
  }
  // Rows before the failing one may or may not have been delivered.
  EXPECT_LE(rows, 2);
  EXPECT_EQ(ret, SQL_ERROR);
  Diag diag = getDiag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(diag.ret, SQL_SUCCESS);
  EXPECT_NE(diag.message.find("Division by zero"), std::string::npos)
      << diag.message;
}

// ---------------------------------------------------------------------------
// Bound character buffers accept every Trino type.
// ---------------------------------------------------------------------------
TEST_F(StatementLifecycleTest, BindCharToNonStringTypes) {
  execDirect("SELECT BIGINT '42', true, DOUBLE '1.5', ARRAY[1, 2, 3], "
             "CAST(ROW(1, 'a') AS ROW(x INTEGER, y VARCHAR))");
  char values[5][64] = {};
  SQLLEN inds[5]     = {};
  for (SQLUSMALLINT i = 0; i < 5; i++) {
    ASSERT_EQ(
        SQLBindCol(
            hStmt, i + 1, SQL_C_CHAR, values[i], sizeof(values[i]), &inds[i]),
        SQL_SUCCESS);
  }
  SQLRETURN ret = SQLFetch(hStmt);
  maybeReportStatementError(ret);
  ASSERT_EQ(ret, SQL_SUCCESS);
  EXPECT_STREQ(values[0], "42");
  EXPECT_EQ(inds[0], 2);
  EXPECT_STREQ(values[1], "1");
  EXPECT_STREQ(values[2], "1.5");
  EXPECT_STREQ(values[3], "[1,2,3]");
  EXPECT_STREQ(values[4], "[1,\"a\"]");
}

TEST_F(StatementLifecycleTest, BindWideCharToNonStringTypes) {
  execDirect("SELECT BIGINT '-7', false");
  SQLWCHAR number[16] = {};
  SQLWCHAR flag[16]   = {};
  SQLLEN numberInd    = 0;
  SQLLEN flagInd      = 0;
  ASSERT_EQ(
      SQLBindCol(hStmt, 1, SQL_C_WCHAR, number, sizeof(number), &numberInd),
      SQL_SUCCESS);
  ASSERT_EQ(SQLBindCol(hStmt, 2, SQL_C_WCHAR, flag, sizeof(flag), &flagInd),
            SQL_SUCCESS);
  ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
  EXPECT_EQ(toWide(number), L"-7");
  EXPECT_EQ(numberInd, static_cast<SQLLEN>(2 * sizeof(SQLWCHAR)));
  EXPECT_EQ(toWide(flag), L"0");
}

// ---------------------------------------------------------------------------
// SQL_ATTR_ROWS_FETCHED_PTR / SQL_ATTR_ROW_STATUS_PTR / rowset size.
// ---------------------------------------------------------------------------
TEST_F(StatementLifecycleTest, RowsFetchedAndRowStatus) {
  SQLULEN rowsFetched    = 99;
  SQLUSMALLINT rowStatus = 99;
  ASSERT_EQ(SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &rowsFetched, 0),
            SQL_SUCCESS);
  ASSERT_EQ(SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_STATUS_PTR, &rowStatus, 0),
            SQL_SUCCESS);

  // The driver fetches one row at a time; a larger rowset is refused with
  // 01S02 and the attribute keeps reporting 1.
  SQLRETURN ret = SQLSetStmtAttr(
      hStmt, SQL_ATTR_ROW_ARRAY_SIZE, reinterpret_cast<SQLPOINTER>(10), 0);
  EXPECT_EQ(ret, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(diagState(), "01S02");
  SQLULEN arraySize = 0;
  ASSERT_EQ(
      SQLGetStmtAttr(hStmt, SQL_ATTR_ROW_ARRAY_SIZE, &arraySize, 0, nullptr),
      SQL_SUCCESS);
  EXPECT_EQ(arraySize, 1u);

  execDirect("SELECT x FROM UNNEST(sequence(1, 3)) AS t(x)");
  for (SQLULEN expectedRow = 1; expectedRow <= 3; expectedRow++) {
    rowsFetched = 99;
    rowStatus   = 99;
    ASSERT_EQ(SQLFetch(hStmt), SQL_SUCCESS);
    EXPECT_EQ(rowsFetched, 1u);
    EXPECT_EQ(rowStatus, SQL_ROW_SUCCESS);
    SQLULEN rowNumber = 0;
    ASSERT_EQ(
        SQLGetStmtAttr(hStmt, SQL_ATTR_ROW_NUMBER, &rowNumber, 0, nullptr),
        SQL_SUCCESS);
    EXPECT_EQ(rowNumber, expectedRow);
  }
  rowsFetched = 99;
  ASSERT_EQ(SQLFetch(hStmt), SQL_NO_DATA);
  EXPECT_EQ(rowsFetched, 0u);
}

// ---------------------------------------------------------------------------
// DOUBLE/REAL special values (sent by Trino as JSON strings) and conversion
// errors.
// ---------------------------------------------------------------------------
TEST_F(StatementLifecycleTest, BindDoubleSpecialValues) {
  execDirect("SELECT nan(), infinity(), -infinity(), REAL 'NaN', "
             "CAST(infinity() AS REAL), DECIMAL '12.50'");
  double d[3]   = {};
  float f[2]    = {};
  double dec    = 0;
  SQLLEN ind[6] = {};
  for (SQLUSMALLINT i = 0; i < 3; i++) {
    ASSERT_EQ(SQLBindCol(hStmt, i + 1, SQL_C_DOUBLE, &d[i], 0, &ind[i]),
              SQL_SUCCESS);
  }
  ASSERT_EQ(SQLBindCol(hStmt, 4, SQL_C_FLOAT, &f[0], 0, &ind[3]), SQL_SUCCESS);
  ASSERT_EQ(SQLBindCol(hStmt, 5, SQL_C_FLOAT, &f[1], 0, &ind[4]), SQL_SUCCESS);
  ASSERT_EQ(SQLBindCol(hStmt, 6, SQL_C_DOUBLE, &dec, 0, &ind[5]), SQL_SUCCESS);
  SQLRETURN ret = SQLFetch(hStmt);
  maybeReportStatementError(ret);
  ASSERT_EQ(ret, SQL_SUCCESS);
  EXPECT_TRUE(std::isnan(d[0]));
  EXPECT_TRUE(std::isinf(d[1]) && d[1] > 0);
  EXPECT_TRUE(std::isinf(d[2]) && d[2] < 0);
  EXPECT_TRUE(std::isnan(f[0]));
  EXPECT_TRUE(std::isinf(f[1]) && f[1] > 0);
  EXPECT_DOUBLE_EQ(dec, 12.5);
  EXPECT_EQ(ind[0], static_cast<SQLLEN>(sizeof(double)));
  EXPECT_EQ(ind[3], static_cast<SQLLEN>(sizeof(float)));

  // SQLGetData converts the same way.
  double value = 0;
  ASSERT_EQ(SQLGetData(hStmt, 3, SQL_C_DOUBLE, &value, 0, nullptr),
            SQL_SUCCESS);
  EXPECT_TRUE(std::isinf(value) && value < 0);
}

TEST_F(StatementLifecycleTest, ConversionErrorsAreReported) {
  execDirect("SELECT 'abc', 300, ARRAY[1]");
  double d   = 0;
  SQLLEN ind = 0;
  ASSERT_EQ(SQLBindCol(hStmt, 1, SQL_C_DOUBLE, &d, 0, &ind), SQL_SUCCESS);
  SQLULEN rowsFetched    = 99;
  SQLUSMALLINT rowStatus = 99;
  ASSERT_EQ(SQLSetStmtAttr(hStmt, SQL_ATTR_ROWS_FETCHED_PTR, &rowsFetched, 0),
            SQL_SUCCESS);
  ASSERT_EQ(SQLSetStmtAttr(hStmt, SQL_ATTR_ROW_STATUS_PTR, &rowStatus, 0),
            SQL_SUCCESS);
  EXPECT_EQ(SQLFetch(hStmt), SQL_ERROR);
  EXPECT_EQ(diagState(), "22018");
  EXPECT_EQ(rowsFetched, 1u);
  EXPECT_EQ(rowStatus, SQL_ROW_ERROR);

  // The row is still current; SQLGetData reports its own conversion errors.
  signed char tiny = 0;
  EXPECT_EQ(SQLGetData(hStmt, 2, SQL_C_STINYINT, &tiny, 0, &ind), SQL_ERROR);
  EXPECT_EQ(diagState(), "22003");
  SQLINTEGER number = 0;
  EXPECT_EQ(SQLGetData(hStmt, 3, SQL_C_SLONG, &number, 0, &ind), SQL_ERROR);
  EXPECT_EQ(diagState(), "07006");
  EXPECT_EQ(SQLGetData(hStmt, 2, SQL_C_SLONG, &number, 0, &ind), SQL_SUCCESS);
  EXPECT_EQ(number, 300);
}

// ---------------------------------------------------------------------------
// Small application buffers are never overrun.
// ---------------------------------------------------------------------------
TEST_F(StatementLifecycleTest, GetInfoServerNameSmallBuffer) {
  SQLSMALLINT fullLength = 0;
  ASSERT_EQ(SQLGetInfo(hDbc, SQL_SERVER_NAME, nullptr, 0, &fullLength),
            SQL_SUCCESS);
  ASSERT_GT(fullLength, 3);

  char buffer[16];
  std::memset(buffer, 'x', sizeof(buffer));
  SQLSMALLINT length = 0;
  EXPECT_EQ(SQLGetInfo(hDbc, SQL_SERVER_NAME, buffer, 4, &length),
            SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(length, fullLength);
  EXPECT_EQ(std::strlen(buffer), 3u);
  for (size_t i = 4; i < sizeof(buffer); i++) {
    EXPECT_EQ(buffer[i], 'x') << "byte " << i << " was overwritten";
  }
  EXPECT_EQ(getDiag(SQL_HANDLE_DBC, hDbc).state, "01004");

  std::string full(static_cast<size_t>(fullLength) + 1, '\0');
  EXPECT_EQ(SQLGetInfo(hDbc,
                       SQL_SERVER_NAME,
                       full.data(),
                       static_cast<SQLSMALLINT>(full.size()),
                       &length),
            SQL_SUCCESS);
  EXPECT_EQ(full.substr(0, 3), std::string(buffer));
  EXPECT_EQ(getDiag(SQL_HANDLE_DBC, hDbc).ret, SQL_NO_DATA);
}

TEST_F(StatementLifecycleTest, ColAttributeAndDescribeColSmallBuffer) {
  execDirect("SELECT 1 AS a_rather_long_column_name");
  char buffer[16];
  std::memset(buffer, 'x', sizeof(buffer));
  SQLSMALLINT length = 0;
  EXPECT_EQ(
      SQLColAttribute(hStmt, 1, SQL_DESC_NAME, buffer, 5, &length, nullptr),
      SQL_SUCCESS_WITH_INFO);
  EXPECT_STREQ(buffer, "a_ra");
  EXPECT_EQ(buffer[5], 'x');
  EXPECT_EQ(length, 25);
  EXPECT_EQ(diagState(), "01004");

  std::memset(buffer, 'x', sizeof(buffer));
  SQLSMALLINT nameLength = 0;
  EXPECT_EQ(SQLDescribeCol(hStmt,
                           1,
                           (SQLCHAR*)buffer,
                           6,
                           &nameLength,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr),
            SQL_SUCCESS_WITH_INFO);
  EXPECT_STREQ(buffer, "a_rat");
  EXPECT_EQ(buffer[6], 'x');
  EXPECT_EQ(nameLength, 25);

  // A zero-length buffer is not written at all.
  std::memset(buffer, 'x', sizeof(buffer));
  EXPECT_EQ(SQLDescribeCol(hStmt,
                           1,
                           (SQLCHAR*)buffer,
                           0,
                           &nameLength,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr),
            SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(buffer[0], 'x');

  SQLLEN count = 0;
  EXPECT_EQ(
      SQLColAttribute(hStmt, 0, SQL_DESC_COUNT, nullptr, 0, nullptr, &count),
      SQL_SUCCESS);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(
      SQLColAttribute(hStmt, 2, SQL_DESC_NAME, buffer, 5, &length, nullptr),
      SQL_ERROR);
  EXPECT_EQ(diagState(), "07009");

  EXPECT_EQ(
      SQLDescribeCol(
          hStmt, 2, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr),
      SQL_ERROR);
  EXPECT_EQ(diagState(), "07009");
}

TEST_F(StatementLifecycleTest, GetDiagRecSmallBuffer) {
  SQLULEN value = 0;
  ASSERT_EQ(SQLGetStmtAttr(hStmt, 123456, &value, 0, nullptr), SQL_ERROR);

  Diag full = getDiag(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(full.ret, SQL_SUCCESS);
  ASSERT_GT(full.message.size(), 5u);

  Diag small = getDiag(SQL_HANDLE_STMT, hStmt, 1, 5);
  EXPECT_EQ(small.ret, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(small.state, "HY092");
  EXPECT_EQ(small.message, full.message.substr(0, 4));
  EXPECT_EQ(small.textLength, static_cast<SQLSMALLINT>(full.message.size()));

  // Invalid arguments.
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt, 0).ret, SQL_ERROR);
  EXPECT_EQ(getDiag(SQL_HANDLE_STMT, hStmt, 1, -1).ret, SQL_ERROR);

  // Connection records are bounded too.
  char info[4] = {};
  ASSERT_EQ(SQLGetInfo(hDbc, 65000, info, sizeof(info), nullptr), SQL_ERROR);
  Diag connFull = getDiag(SQL_HANDLE_DBC, hDbc);
  ASSERT_EQ(connFull.ret, SQL_SUCCESS);
  Diag connSmall = getDiag(SQL_HANDLE_DBC, hDbc, 1, 8);
  EXPECT_EQ(connSmall.ret, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(connSmall.message, connFull.message.substr(0, 7));
  EXPECT_EQ(connSmall.textLength,
            static_cast<SQLSMALLINT>(connFull.message.size()));
  EXPECT_EQ(getDiag(SQL_HANDLE_DBC, hDbc, 2).ret, SQL_NO_DATA);
}

TEST_F(StatementLifecycleTest, ServerErrorSplitIntoSmallRecords) {
  const std::string bad = "SELECT * FROM tpch.tiny.no_such_table";
  SQLRETURN ret         = SQLExecDirect(hStmt, (SQLCHAR*)bad.c_str(), SQL_NTS);
  if (ret == SQL_SUCCESS) {
    SQLSMALLINT columns = 0;
    ret                 = SQLNumResultCols(hStmt, &columns);
  }
  ASSERT_EQ(ret, SQL_ERROR);

  // The length query reports the whole message as one record.
  SQLSMALLINT totalLength = 0;
  ASSERT_EQ(SQLGetDiagRec(SQL_HANDLE_STMT,
                          hStmt,
                          1,
                          nullptr,
                          nullptr,
                          nullptr,
                          0,
                          &totalLength),
            SQL_SUCCESS);
  ASSERT_GT(totalLength, 0);

  // With a small buffer the message is split over several records, each a
  // complete SQLSTATE plus a piece of text that fits.
  constexpr SQLSMALLINT bufferLength = 24;
  std::string joined;
  SQLSMALLINT rec = 1;
  for (; rec < 1000; rec++) {
    Diag diag = getDiag(SQL_HANDLE_STMT, hStmt, rec, bufferLength);
    if (diag.ret == SQL_NO_DATA) {
      break;
    }
    ASSERT_EQ(diag.ret, SQL_SUCCESS) << "record " << rec;
    EXPECT_EQ(diag.state.size(), 5u);
    EXPECT_LT(diag.message.size(), static_cast<size_t>(bufferLength));
    joined += diag.message;
  }
  EXPECT_GT(rec, 2);
  EXPECT_LT(rec, 1000);
  // Long lines are split; drop the line breaks before searching.
  std::erase(joined, '\n');
  EXPECT_NE(joined.find("no_such_table"), std::string::npos) << joined;
}
