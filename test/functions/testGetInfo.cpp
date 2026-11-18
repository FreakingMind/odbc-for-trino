#include "../../src/util/windowsLean.hpp"

#include <gtest/gtest.h>
#include <sql.h>
#include <sqlext.h>

#include "../constants.hpp"
#include "../fixtures/sqlDriverConnectFixture.hpp"

class GetInfoTest : public SQLDriverConnectFixture {};

TEST_F(GetInfoTest, GetODBCVersion) {
  unsigned char buf[16];
  // Put some nonsense into the char array as a test.
  std::fill_n(buf, 16, 'A');
  SQLSMALLINT bufferLen        = 16;
  SQLSMALLINT StrLen_or_IndPtr = 0;
  SQLRETURN ret                = SQLGetInfo(
      this->hDbc, SQL_DRIVER_ODBC_VER, buf, bufferLen, &StrLen_or_IndPtr);

  // The string should be 5 long. The spec says the string length
  // is returned _excluding_ the null-termination character.
  ASSERT_EQ(StrLen_or_IndPtr, 5);
  // We can use the returned length to construct a proper string
  // from the char array to do this comparison.
  std::string odbcVer(buf, buf + StrLen_or_IndPtr);
  ASSERT_EQ(odbcVer, std::string("03.80"));
}

TEST_F(GetInfoTest, GetTrinoDBMSName) {
  unsigned char buf[16];
  // Put some nonsense into the char array as a test.
  std::fill_n(buf, 16, 'A');
  SQLSMALLINT bufferLen        = 16;
  SQLSMALLINT StrLen_or_IndPtr = 0;
  SQLRETURN ret =
      SQLGetInfo(this->hDbc, SQL_DBMS_NAME, buf, bufferLen, &StrLen_or_IndPtr);

  // The string should be 5 long. The spec says the string length
  // is returned _excluding_ the null-termination character.
  ASSERT_EQ(StrLen_or_IndPtr, 5);

  // We can use the returned length to construct a proper string
  // from the char array to do this comparison.
  std::string dbmsName(buf, buf + StrLen_or_IndPtr);
  ASSERT_EQ(dbmsName, std::string("Trino"));
}

TEST_F(GetInfoTest, GetTrinoDBMSVersion) {
  unsigned char buf[16];
  // Put some nonsense into the char array as a test.
  std::fill_n(buf, 16, 'A');
  SQLSMALLINT bufferLen        = 16;
  SQLSMALLINT StrLen_or_IndPtr = 0;
  SQLRETURN ret =
      SQLGetInfo(this->hDbc, SQL_DBMS_VER, buf, bufferLen, &StrLen_or_IndPtr);

  // The format of the version string is `##.##.#### <optional version>`.
  // The internal fixes deliberately report the plain `00.00.0001`, without
  // upstream's Trino server version suffix.
  ASSERT_EQ(StrLen_or_IndPtr, 10);

  // We can use the returned length to construct a proper string
  // from the char array to do this comparison.
  std::string dbmsVers(buf, buf + StrLen_or_IndPtr);
  ASSERT_EQ(dbmsVers, std::string("00.00.0001"));
}

TEST_F(GetInfoTest, GetSQLServerName) {
  // Put some nonsense into the char array as a test.
  unsigned char buf[32]        = {'A'};
  SQLSMALLINT bufferLen        = sizeof(buf);
  SQLSMALLINT StrLen_or_IndPtr = 0;
  SQLRETURN ret                = SQLGetInfo(
      this->hDbc, SQL_SERVER_NAME, buf, bufferLen, &StrLen_or_IndPtr);

  // The string is probably "localhost" and the length is 9 characters
  // excluding the null termination character.
  ASSERT_EQ(StrLen_or_IndPtr, 9);

  // We can use the returned length to construct a proper string
  // from the char array to do this comparison.
  std::string serverName(buf, buf + StrLen_or_IndPtr);
  ASSERT_EQ(serverName, std::string("localhost"));
}
