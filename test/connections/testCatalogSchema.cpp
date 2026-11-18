#include "../../src/util/windowsLean.hpp"

#include <sql.h>
#include <sqlext.h>

#include <gtest/gtest.h>
#include <string>

#include "../constants.hpp"
#include "../fixtures/sqlDriverConnectFixture.hpp"

/*
The catalog / schema connection options, USE, SQL_ATTR_CURRENT_CATALOG and
SQLSetConnectOption. Runs against the Trino of the test DSN (tpch catalog).
*/

namespace {

class CatalogSchemaTest : public SQLDriverConnectFixture {
  protected:
    // Each test connects with its own options.
    void SetUp() override {}

    // The single value of a one-row query, as text; "" on failure.
    std::string scalar(const std::string& query) {
      SQLHSTMT stmt = nullptr;
      EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &stmt), SQL_SUCCESS);
      SQLRETURN ret = SQLExecDirect(stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
      SQLSMALLINT columns = 0;
      if (SQL_SUCCEEDED(ret)) {
        SQLNumResultCols(stmt, &columns);
        if (columns == 0) { // USE, SET SESSION, ...
          SQLFreeHandle(SQL_HANDLE_STMT, stmt);
          return "";
        }
        ret = SQLFetch(stmt);
      }
      std::string value;
      if (SQL_SUCCEEDED(ret)) {
        char buffer[256] = {};
        SQLLEN indicator = 0;
        if (SQL_SUCCEEDED(SQLGetData(
                stmt, 1, SQL_C_CHAR, buffer, sizeof(buffer), &indicator))) {
          value = buffer;
        }
      } else {
        this->hStmt = stmt;
        maybeReportStatementError(ret);
        this->hStmt = nullptr;
      }
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      return value;
    }

    // The current catalog (SQL_ATTR_CURRENT_CATALOG).
    std::string currentCatalog() {
      char buffer[128]  = {};
      SQLINTEGER length = 0;
      EXPECT_EQ(
          SQLGetConnectAttr(
              hDbc, SQL_ATTR_CURRENT_CATALOG, buffer, sizeof(buffer), &length),
          SQL_SUCCESS);
      return buffer;
    }

    // Allocates an unconnected handle in hDbc.
    void allocate() {
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc), SQL_SUCCESS);
    }

    SQLRETURN connect(const std::string& options) {
      const std::string connStr = "DSN=" + TEST_DSN + ";" + options;
      return SQLDriverConnect(hDbc,
                              nullptr,
                              (SQLCHAR*)connStr.c_str(),
                              SQL_NTS,
                              nullptr,
                              0,
                              nullptr,
                              SQL_DRIVER_NOPROMPT);
    }
};

} // namespace

TEST_F(CatalogSchemaTest, UnqualifiedNamesUseCatalogAndSchema) {
  SQLDriverConnectFixture::SetUp("catalog=tpch;schema=tiny");
  EXPECT_EQ(scalar("SELECT count(*) FROM nation"), "25");
  EXPECT_EQ(scalar("SELECT count(*) FROM sf1.region"), "5");
  EXPECT_EQ(currentCatalog(), "tpch");
}

TEST_F(CatalogSchemaTest, CatalogAloneQualifiesSchemaNames) {
  SQLDriverConnectFixture::SetUp("catalog=tpch");
  EXPECT_EQ(scalar("SELECT count(*) FROM tiny.region"), "5");
}

TEST_F(CatalogSchemaTest, SchemaWithoutCatalogIsRejected) {
  allocate();
  EXPECT_EQ(connect("schema=tiny"), SQL_ERROR);
}

TEST_F(CatalogSchemaTest, UseChangesTheSession) {
  SQLDriverConnectFixture::SetUp("");
  EXPECT_EQ(scalar("USE tpch.tiny"), "");
  EXPECT_EQ(scalar("SELECT count(*) FROM region"), "5");
  EXPECT_EQ(currentCatalog(), "tpch");
}

TEST_F(CatalogSchemaTest, CurrentCatalogAttributeBeforeConnecting) {
  allocate();
  const std::string catalog = "tpch";
  ASSERT_EQ(
      SQLSetConnectAttr(
          hDbc, SQL_ATTR_CURRENT_CATALOG, (SQLPOINTER)catalog.c_str(), SQL_NTS),
      SQL_SUCCESS);
  ASSERT_EQ(connect(""), SQL_SUCCESS);
  EXPECT_EQ(currentCatalog(), "tpch");
  EXPECT_EQ(scalar("SELECT count(*) FROM tiny.region"), "5");
}

TEST_F(CatalogSchemaTest, Odbc2SetConnectOption) {
  allocate();
  // Excel for Mac sets the login timeout this way before connecting.
  EXPECT_EQ(SQLSetConnectOption(hDbc, SQL_LOGIN_TIMEOUT, 45), SQL_SUCCESS);
  SQLUINTEGER timeout = 0;
  EXPECT_EQ(
      SQLGetConnectAttr(
          hDbc, SQL_ATTR_LOGIN_TIMEOUT, &timeout, sizeof(timeout), nullptr),
      SQL_SUCCESS);
  EXPECT_EQ(timeout, 45U);
}
