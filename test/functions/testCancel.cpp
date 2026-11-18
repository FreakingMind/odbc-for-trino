#include "../../src/util/windowsLean.hpp"

#include <gtest/gtest.h>
#include <iostream>
#include <sql.h>
#include <sqlext.h>

#include "../constants.hpp"

#include "../fixtures/sqlDriverConnectFixture.hpp"

class SQLCancelTest : public SQLDriverConnectFixture {};

TEST_F(SQLCancelTest, TestCancelQuery) {
  // Allocate statement handle
  SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
  ASSERT_EQ(ret, SQL_SUCCESS);

  // Execute the query, one that will take a long time to fully return.
  // It streams rows rather than building a large join: that join exceeds
  // the memory limit of a small (Docker) Trino, and the failure is now
  // reported by SQLFetch instead of looking like the end of the data.
  std::string query = R"SQL(
      SELECT *
      FROM tpch.sf100.lineitem
  )SQL";
  ret               = SQLExecDirect(hStmt, (SQLCHAR*)query.c_str(), SQL_NTS);
  ASSERT_EQ(ret, SQL_SUCCESS);

  // Bind the column to a bigint variable
  SQLSMALLINT cType   = SQL_C_SBIGINT;
  SQLBIGINT result    = 0;
  SQLLEN indicator    = 0;
  SQLLEN bufferLength = -1; // Ignored for integer types.
  ret = SQLBindCol(hStmt, 1, cType, &result, bufferLength, &indicator);
  ASSERT_EQ(ret, SQL_SUCCESS);

  // Fetch a tiny bit of the data
  ret = SQLFetch(hStmt);
  ASSERT_TRUE(ret == SQL_SUCCESS or ret == SQL_NO_DATA);

  // Now cancel the query
  ret = SQLCancel(hStmt);
  ASSERT_EQ(ret, SQL_SUCCESS);

  // Free statement handle
  ret = SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
  ASSERT_EQ(ret, SQL_SUCCESS);
}
