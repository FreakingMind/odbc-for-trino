#include "../../src/util/windowsLean.hpp"

#include <sql.h>
#include <sqlext.h>

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "../constants.hpp"
#include "../fixtures/sqlDriverConnectFixture.hpp"

/*
The ansiEncoding connection option: the encoding of SQL_C_CHAR result data.
Runs against the Trino of the test DSN.
*/

namespace {

// "Привет" in UTF-8 and in windows-1251.
const std::string PRIVET_UTF8 =
    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
const std::string PRIVET_CP1251 = "\xCF\xF0\xE8\xE2\xE5\xF2";

// U+1F600 in UTF-8 and GB18030 (4 bytes each); "中" in UTF-8 and GB18030.
const std::string EMOJI_UTF8    = "\xF0\x9F\x98\x80";
const std::string EMOJI_GB18030 = "\x94\x39\xFC\x36";
const std::string ZHONG_UTF8    = "\xE4\xB8\xAD";
const std::string ZHONG_GB18030 = "\xD6\xD0";

// The parts SQLGetData returned for one value.
struct ChunkedValue {
    std::vector<std::string> parts;
    std::vector<SQLRETURN> returns;

    std::string joined() const {
      std::string result;
      for (const std::string& part : parts) {
        result += part;
      }
      return result;
    }
};

class AnsiEncodingConnectionTest : public SQLDriverConnectFixture {
  protected:
    // Each test connects with its own options.
    void SetUp() override {}

    static std::string statementDiagnostics(SQLHSTMT stmt) {
      SQLCHAR state[6]      = {};
      SQLCHAR message[1024] = {};
      SQLINTEGER native     = 0;
      SQLSMALLINT length    = 0;
      if (SQLGetDiagRec(SQL_HANDLE_STMT,
                        stmt,
                        1,
                        state,
                        &native,
                        message,
                        sizeof(message),
                        &length) != SQL_SUCCESS) {
        return "(no diagnostics)";
      }
      return std::string(reinterpret_cast<char*>(state)) + " " +
             reinterpret_cast<char*>(message);
    }

    // Executes a query on a new statement of hDbcToUse and fetches the
    // first row.
    SQLHSTMT execute(SQLHDBC hDbcToUse, const std::string& query) {
      SQLHSTMT stmt = nullptr;
      EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbcToUse, &stmt), SQL_SUCCESS);
      const SQLRETURN ret =
          SQLExecDirect(stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
      EXPECT_EQ(ret, SQL_SUCCESS) << statementDiagnostics(stmt);
      const SQLRETURN fetched = SQLFetch(stmt);
      EXPECT_EQ(fetched, SQL_SUCCESS) << statementDiagnostics(stmt);
      return stmt;
    }

    // One SQLGetData(SQL_C_CHAR) call with a buffer of bufferLength bytes;
    // appends the part to value. Returns the SQLGetData result.
    SQLRETURN
    readPart(SQLHSTMT stmt, SQLLEN bufferLength, ChunkedValue& value) {
      std::vector<char> buffer(static_cast<std::size_t>(bufferLength), '\0');
      SQLLEN indicator    = 0;
      const SQLRETURN ret = SQLGetData(
          stmt, 1, SQL_C_CHAR, buffer.data(), bufferLength, &indicator);
      if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        value.parts.emplace_back(buffer.data(), std::strlen(buffer.data()));
        value.returns.push_back(ret);
      }
      return ret;
    }

    // Reads column 1 of the current row with SQLGetData in parts of at
    // most bufferLength - 1 bytes.
    ChunkedValue readChunked(SQLHSTMT stmt, SQLLEN bufferLength) {
      ChunkedValue value;
      for (int call = 0; call < 10000; call++) {
        const SQLRETURN ret = readPart(stmt, bufferLength, value);
        if (ret != SQL_SUCCESS_WITH_INFO) {
          EXPECT_EQ(ret, SQL_SUCCESS);
          break;
        }
      }
      EXPECT_EQ(readPart(stmt, bufferLength, value), SQL_NO_DATA);
      return value;
    }

    // Runs a single-value query on hDbc and returns it as SQL_C_CHAR bytes.
    std::string fetchChar(const std::string& query) {
      SQLHSTMT stmt      = execute(hDbc, query);
      std::string result = readChunked(stmt, 256).joined();
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);
      return result;
    }

    // Connects another handle on hEnv. Returns the SQLDriverConnect result.
    SQLRETURN connectOther(SQLHDBC* other, const std::string& options) {
      EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, hEnv, other), SQL_SUCCESS);
      const std::string connStr = "DSN=" + TEST_DSN + ";" + options;
      return SQLDriverConnect(*other,
                              nullptr,
                              (SQLCHAR*)connStr.c_str(),
                              SQL_NTS,
                              nullptr,
                              0,
                              nullptr,
                              SQL_DRIVER_NOPROMPT);
    }

    // Connecting with these options fails with a message about
    // ansiEncoding.
    void expectConnectFails(const std::string& options) {
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
      EXPECT_EQ(connectOther(&hDbc, options), SQL_ERROR);

      SQLCHAR state[6]     = {};
      SQLCHAR message[512] = {};
      SQLINTEGER native    = 0;
      SQLSMALLINT length   = 0;
      ASSERT_EQ(SQLGetDiagRec(SQL_HANDLE_DBC,
                              hDbc,
                              1,
                              state,
                              &native,
                              message,
                              sizeof(message),
                              &length),
                SQL_SUCCESS);
      EXPECT_NE(
          std::string(reinterpret_cast<char*>(message)).find("ansiEncoding"),
          std::string::npos)
          << message;
    }
};

} // namespace

TEST_F(AnsiEncodingConnectionTest, Utf8) {
  SQLDriverConnectFixture::SetUp("ansiencoding=UTF-8");
  EXPECT_EQ(fetchChar("SELECT '" + PRIVET_UTF8 + "'"), PRIVET_UTF8);
}

TEST_F(AnsiEncodingConnectionTest, Utf8BufferSmallerThanACharacter) {
  SQLDriverConnectFixture::SetUp("ansiEncoding=UTF-8");
  SQLHSTMT stmt = execute(hDbc, "SELECT 'a" + EMOJI_UTF8 + "b'");
  // 2 bytes of payload per call: the 4-byte character is split rather than
  // returning nothing forever.
  const ChunkedValue value = readChunked(stmt, 3);
  EXPECT_EQ(value.joined(), "a" + EMOJI_UTF8 + "b");
  ASSERT_FALSE(value.returns.empty());
  EXPECT_EQ(value.returns.back(), SQL_SUCCESS);
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

TEST_F(AnsiEncodingConnectionTest, UnknownEncodingFailsTheConnection) {
  expectConnectFails("ansiencoding=no-such-charset");
}

#ifdef __APPLE__
TEST_F(AnsiEncodingConnectionTest, Windows1251) {
  SQLDriverConnectFixture::SetUp("ansiencoding= windows-1251 ");
  EXPECT_EQ(fetchChar("SELECT '" + PRIVET_UTF8 + "'"), PRIVET_CP1251);
}

TEST_F(AnsiEncodingConnectionTest,
       UnrepresentableCharactersBecomeQuestionMarks) {
  SQLDriverConnectFixture::SetUp("ansiencoding=macintosh");
  EXPECT_EQ(fetchChar("SELECT 'Caf\xC3\xA9 " + PRIVET_UTF8 + "'"),
            "Caf\x8E ??????");
}

TEST_F(AnsiEncodingConnectionTest, UnsupportedEncodingFailsTheConnection) {
  expectConnectFails("ansiencoding=UTF-16");
}

TEST_F(AnsiEncodingConnectionTest, TwoConnectionsKeepTheirEncodings) {
  SQLDriverConnectFixture::SetUp("ansiencoding=windows-1251");

  std::string longText;
  std::string longCp1251;
  for (int i = 0; i < 20; i++) {
    longText += PRIVET_UTF8 + " ";
    longCp1251 += PRIVET_CP1251 + " ";
  }

  // Start a chunked read on the windows-1251 connection...
  SQLHSTMT cp1251Stmt = execute(hDbc, "SELECT '" + longText + "'");
  ChunkedValue cp1251Value;
  ASSERT_EQ(readPart(cp1251Stmt, 11, cp1251Value), SQL_SUCCESS_WITH_INFO);

  // ...connect with another encoding and read in between...
  SQLHDBC utf8Dbc = nullptr;
  EXPECT_EQ(connectOther(&utf8Dbc, "ansiencoding=UTF-8"), SQL_SUCCESS);
  SQLHSTMT utf8Stmt = execute(utf8Dbc, "SELECT '" + PRIVET_UTF8 + "'");
  EXPECT_EQ(readChunked(utf8Stmt, 64).joined(), PRIVET_UTF8);
  SQLFreeHandle(SQL_HANDLE_STMT, utf8Stmt);
  SQLDisconnect(utf8Dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, utf8Dbc);

  // ...and finish the first one: still windows-1251.
  const ChunkedValue rest = readChunked(cp1251Stmt, 11);
  EXPECT_EQ(cp1251Value.joined() + rest.joined(), longCp1251);
  SQLFreeHandle(SQL_HANDLE_STMT, cp1251Stmt);
}

TEST_F(AnsiEncodingConnectionTest, ChunkedReadInALegacyEncoding) {
  SQLDriverConnectFixture::SetUp("ansiencoding=windows-1251");
  std::string text;
  std::string expected;
  for (int i = 0; i < 30; i++) {
    text += PRIVET_UTF8;
    expected += PRIVET_CP1251;
  }
  SQLHSTMT stmt            = execute(hDbc, "SELECT '" + text + "'");
  const ChunkedValue value = readChunked(stmt, 7);
  EXPECT_EQ(value.joined(), expected);
  // 6 bytes per part: one byte per Cyrillic letter.
  EXPECT_EQ(value.parts.size(), expected.size() / 6);
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

TEST_F(AnsiEncodingConnectionTest, ChunkedReadInAMultiByteEncoding) {
  SQLDriverConnectFixture::SetUp("ansiencoding=GB18030");
  const std::string text = "a" + EMOJI_UTF8 + ZHONG_UTF8 + EMOJI_UTF8;
  const std::string expected =
      "a" + EMOJI_GB18030 + ZHONG_GB18030 + EMOJI_GB18030;

  // 4 bytes per part: parts end on character boundaries.
  SQLHSTMT stmt            = execute(hDbc, "SELECT '" + text + "'");
  const ChunkedValue value = readChunked(stmt, 5);
  EXPECT_EQ(value.joined(), expected);
  ASSERT_EQ(value.parts.size(), 4U);
  EXPECT_EQ(value.parts[0], "a");
  EXPECT_EQ(value.parts[1], EMOJI_GB18030);
  EXPECT_EQ(value.parts[2], ZHONG_GB18030);
  EXPECT_EQ(value.parts[3], EMOJI_GB18030);
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);

  // 2 bytes per part: smaller than the 4-byte characters, still complete.
  stmt = execute(hDbc, "SELECT '" + text + "'");
  EXPECT_EQ(readChunked(stmt, 3).joined(), expected);
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}
TEST_F(AnsiEncodingConnectionTest, BoundColumnThenGetDataInAMultiByteEncoding) {
  SQLDriverConnectFixture::SetUp("ansiencoding=GB18030");
  const std::string text = "a" + EMOJI_UTF8 + ZHONG_UTF8 + EMOJI_UTF8;
  const std::string expected =
      "a" + EMOJI_GB18030 + ZHONG_GB18030 + EMOJI_GB18030;

  SQLHSTMT stmt = nullptr;
  ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &stmt), SQL_SUCCESS);
  // 3 bytes of payload: SQLFetch copies "a" (the 4-byte character after it
  // doesn't fit), like Excel's small preview buffer.
  char bound[4]   = {};
  SQLLEN boundInd = 0;
  ASSERT_EQ(SQLBindCol(stmt, 1, SQL_C_CHAR, bound, sizeof(bound), &boundInd),
            SQL_SUCCESS);
  const std::string query = "SELECT '" + text + "'";
  ASSERT_EQ(SQLExecDirect(stmt, (SQLCHAR*)query.c_str(), SQL_NTS), SQL_SUCCESS);
  ASSERT_EQ(SQLFetch(stmt), SQL_SUCCESS);
  EXPECT_EQ(std::string(bound), "a");
  EXPECT_EQ(boundInd, static_cast<SQLLEN>(expected.size()));

  // SQLGetData continues after the bytes SQLFetch returned.
  const ChunkedValue rest = readChunked(stmt, 5);
  EXPECT_EQ("a" + rest.joined(), expected);
  ASSERT_EQ(rest.parts.size(), 3U);
  EXPECT_EQ(rest.parts[0], EMOJI_GB18030);
  EXPECT_EQ(rest.parts[1], ZHONG_GB18030);
  EXPECT_EQ(rest.parts[2], EMOJI_GB18030);
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}
#endif
