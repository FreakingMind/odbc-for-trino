#include "gtest/gtest.h"

#include <climits>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "../../../src/driver/unicode/stringAttributes.hpp"
#include "../../../src/driver/unicode/unicodeWrappers.hpp"
#include "../../../src/driver/unicode/wideText.hpp"
#include "../../../src/platform/textEncoding.hpp"

/*
Unit tests for the Unicode (W) API helpers. The char16_t / char32_t tests
cover both SQLWCHAR encodings on every platform; the SQLWCHAR tests are
written to hold for either size.
*/

namespace {

// "Привет, мир": 11 characters, 20 UTF-8 bytes.
const std::string CYRILLIC =
    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, "
    "\xD0\xBC\xD0\xB8\xD1\x80";
// U+1F600: 4 UTF-8 bytes, a surrogate pair in UTF-16.
const std::string EMOJI = "\xF0\x9F\x98\x80";
// "中": 3 UTF-8 bytes.
const std::string CJK = "\xE4\xB8\xAD";

std::vector<SQLWCHAR> toSqlWide(const std::string& utf8) {
  const SqlWideString wide = utf8ToSqlWide(utf8);
  std::vector<SQLWCHAR> result(wide.size() + 1, 0);
  std::memcpy(result.data(), wide.data(), wide.size() * sizeof(SQLWCHAR));
  return result;
}

std::string fromSqlWide(const SQLWCHAR* text) {
  return sqlWideToUtf8(text, SQL_NTS);
}

constexpr std::size_t EMOJI_UNITS = sizeof(SQLWCHAR) == 2 ? 2 : 1;

} // namespace

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 / UTF-32

TEST(WideTextTest, Utf8ToUtf32) {
  EXPECT_EQ(utf8ToUtf<char32_t>(CYRILLIC), U"Привет, мир");
  EXPECT_EQ(utf8ToUtf<char32_t>(EMOJI), U"\U0001F600");
  EXPECT_EQ(utf8ToUtf<char32_t>(CJK), U"中");
  EXPECT_EQ(utf8ToUtf<char32_t>(""), U"");
}

TEST(WideTextTest, Utf8ToUtf16UsesSurrogatePairs) {
  EXPECT_EQ(utf8ToUtf<char16_t>(CYRILLIC), u"Привет, мир");
  const std::u16string emoji = utf8ToUtf<char16_t>(EMOJI);
  ASSERT_EQ(emoji.size(), 2u);
  EXPECT_EQ(emoji[0], 0xD83D);
  EXPECT_EQ(emoji[1], 0xDE00);
}

TEST(WideTextTest, RoundTripsThroughBothEncodings) {
  const std::string text = "A" + CYRILLIC + CJK + EMOJI + "z";
  EXPECT_EQ(utfToUtf8<char16_t>(utf8ToUtf<char16_t>(text)), text);
  EXPECT_EQ(utfToUtf8<char32_t>(utf8ToUtf<char32_t>(text)), text);
}

TEST(WideTextTest, InvalidUtf8BecomesReplacementCharacter) {
  // A lone continuation byte, a truncated sequence, an overlong "/" and an
  // encoded surrogate.
  EXPECT_EQ(utf8ToUtf<char32_t>("a\x80z"), U"a�z");
  EXPECT_EQ(utf8ToUtf<char32_t>("a\xD0"), U"a�");
  EXPECT_EQ(utf8ToUtf<char32_t>("\xC0\xAF"), U"��");
  EXPECT_EQ(utf8ToUtf<char32_t>("\xED\xA0\x80"), U"���");
}

TEST(WideTextTest, InvalidWideInputBecomesReplacementCharacter) {
  const std::string replacement = "\xEF\xBF\xBD";
  // Unpaired surrogates in UTF-16.
  const char16_t loneHigh[] = {u'a', 0xD83D, u'z'};
  EXPECT_EQ(utfToUtf8<char16_t>(std::u16string_view(loneHigh, 3)),
            "a" + replacement + "z");
  const char16_t loneLow[] = {0xDE00};
  EXPECT_EQ(utfToUtf8<char16_t>(std::u16string_view(loneLow, 1)), replacement);
  // Surrogates and values above U+10FFFF in UTF-32.
  const char32_t bad[] = {0xD800, 0x110000};
  EXPECT_EQ(utfToUtf8<char32_t>(std::u32string_view(bad, 2)),
            replacement + replacement);
}

TEST(WideTextTest, CharBoundaryKeepsSurrogatePairsTogether) {
  const std::u16string text = u"ab\U0001F600c";
  ASSERT_EQ(text.size(), 5u);
  EXPECT_EQ(utfCharBoundary<char16_t>(text, 2), 2u);
  // Cutting after the high surrogate would split the pair.
  EXPECT_EQ(utfCharBoundary<char16_t>(text, 3), 2u);
  EXPECT_EQ(utfCharBoundary<char16_t>(text, 4), 4u);
  EXPECT_EQ(utfCharBoundary<char16_t>(text, 100), 5u);
  EXPECT_EQ(utfCharBoundary<char16_t>(text, 0), 0u);

  const std::u32string text32 = U"ab\U0001F600c";
  EXPECT_EQ(utfCharBoundary<char32_t>(text32, 3), 3u);
}

// ---------------------------------------------------------------------------
// SQLWCHAR input and output buffers

TEST(WideTextTest, SqlWideInputLengths) {
  const std::vector<SQLWCHAR> text = toSqlWide(CYRILLIC);
  EXPECT_EQ(sqlWideLength(text.data()), 11u);
  EXPECT_EQ(sqlWideToUtf8(text.data(), SQL_NTS), CYRILLIC);
  // An explicit length counts characters: "Привет".
  EXPECT_EQ(sqlWideToUtf8(text.data(), 6), CYRILLIC.substr(0, 12));
  EXPECT_EQ(sqlWideToUtf8(text.data(), 0), "");
  EXPECT_EQ(sqlWideToUtf8(nullptr, SQL_NTS), "");
}

TEST(WideTextTest, CopyFitsWithTerminator) {
  const SqlWideString text = utf8ToSqlWide(CYRILLIC);
  std::vector<SQLWCHAR> buffer(12, 0x7F);
  const WideCopyResult result =
      copyToSqlWideBuffer(text, buffer.data(), buffer.size());
  EXPECT_FALSE(result.truncated);
  EXPECT_EQ(result.totalUnits, 11u);
  EXPECT_EQ(fromSqlWide(buffer.data()), CYRILLIC);
}

TEST(WideTextTest, CopyTruncatesAndTerminates) {
  const SqlWideString text = utf8ToSqlWide(CYRILLIC);
  std::vector<SQLWCHAR> buffer(7, 0x7F);
  const WideCopyResult result =
      copyToSqlWideBuffer(text, buffer.data(), buffer.size());
  EXPECT_TRUE(result.truncated);
  EXPECT_EQ(result.totalUnits, 11u);
  EXPECT_EQ(buffer[6], 0);
  EXPECT_EQ(fromSqlWide(buffer.data()), CYRILLIC.substr(0, 12)); // "Привет"
}

TEST(WideTextTest, CopyDoesNotSplitFourByteCharacters) {
  const SqlWideString text = utf8ToSqlWide("a" + EMOJI);
  // Room for "a" and one more unit: in UTF-16 that's half the pair.
  std::vector<SQLWCHAR> buffer(3, 0x7F);
  const WideCopyResult result =
      copyToSqlWideBuffer(text, buffer.data(), buffer.size());
  if (sizeof(SQLWCHAR) == 2) {
    EXPECT_TRUE(result.truncated);
    EXPECT_EQ(fromSqlWide(buffer.data()), "a");
  } else {
    EXPECT_FALSE(result.truncated);
    EXPECT_EQ(fromSqlWide(buffer.data()), "a" + EMOJI);
  }
  EXPECT_EQ(result.totalUnits, 1 + EMOJI_UNITS);
}

TEST(WideTextTest, CopyWithNullOrEmptyBuffer) {
  const SqlWideString text = utf8ToSqlWide(CYRILLIC);
  WideCopyResult result    = copyToSqlWideBuffer(text, nullptr, 0);
  EXPECT_FALSE(result.truncated); // Length-only call.
  EXPECT_EQ(result.totalUnits, 11u);

  SQLWCHAR untouched = 0x7F;
  result             = copyToSqlWideBuffer(text, &untouched, 0);
  EXPECT_TRUE(result.truncated);
  EXPECT_EQ(untouched, 0x7F);

  SQLWCHAR terminatorOnly = 0x7F;
  result                  = copyToSqlWideBuffer(text, &terminatorOnly, 1);
  EXPECT_TRUE(result.truncated);
  EXPECT_EQ(terminatorOnly, 0);
}

// ---------------------------------------------------------------------------
// Output length units and the narrow-call helpers

TEST(UnicodeWrappersTest, WriteOutputInCharacters) {
  std::vector<SQLWCHAR> buffer(32, 0x7F);
  SQLSMALLINT length = -1;
  const bool truncated =
      writeWideOutput<SQLSMALLINT>(CYRILLIC,
                                   buffer.data(),
                                   static_cast<SQLSMALLINT>(buffer.size()),
                                   &length,
                                   WideLengthUnit::Characters,
                                   WideTextCodec{});
  EXPECT_FALSE(truncated);
  EXPECT_EQ(length, 11);
  EXPECT_EQ(fromSqlWide(buffer.data()), CYRILLIC);
}

TEST(UnicodeWrappersTest, WriteOutputInBytes) {
  std::vector<SQLWCHAR> buffer(32, 0x7F);
  SQLINTEGER length    = -1;
  const bool truncated = writeWideOutput<SQLINTEGER>(
      CYRILLIC,
      buffer.data(),
      static_cast<SQLINTEGER>(buffer.size() * sizeof(SQLWCHAR)),
      &length,
      WideLengthUnit::Bytes,
      WideTextCodec{});
  EXPECT_FALSE(truncated);
  EXPECT_EQ(length, static_cast<SQLINTEGER>(11 * sizeof(SQLWCHAR)));
  EXPECT_EQ(fromSqlWide(buffer.data()), CYRILLIC);
}

TEST(UnicodeWrappersTest, WriteOutputTruncatesInBytes) {
  // 7 characters of room, plus a stray byte that can't hold a SQLWCHAR.
  std::vector<SQLWCHAR> buffer(8, 0x7F);
  SQLSMALLINT length   = -1;
  const bool truncated = writeWideOutput<SQLSMALLINT>(
      CYRILLIC,
      buffer.data(),
      static_cast<SQLSMALLINT>(7 * sizeof(SQLWCHAR) + 1),
      &length,
      WideLengthUnit::Bytes,
      WideTextCodec{});
  EXPECT_TRUE(truncated);
  // The full length is reported, in bytes.
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(11 * sizeof(SQLWCHAR)));
  EXPECT_EQ(fromSqlWide(buffer.data()), CYRILLIC.substr(0, 12));
  EXPECT_EQ(buffer[7], 0x7F);
}

TEST(UnicodeWrappersTest, WriteOutputLengthOnly) {
  SQLSMALLINT length = -1;
  EXPECT_FALSE(writeWideOutput<SQLSMALLINT>(CYRILLIC + EMOJI,
                                            nullptr,
                                            0,
                                            &length,
                                            WideLengthUnit::Characters,
                                            WideTextCodec{}));
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(11 + EMOJI_UNITS));
}

TEST(UnicodeWrappersTest, FetchNarrowStringWithUnboundedWriter) {
  // Imitates an ANSI function that ignores the buffer size.
  const std::string value(1000, 'x');
  int calls = 0;
  std::string out;
  SQLRETURN rc = fetchNarrowString<SQLSMALLINT>(
      [&](SQLCHAR* buffer, SQLSMALLINT, SQLSMALLINT* length) {
        calls++;
        if (buffer) {
          std::memcpy(buffer, value.c_str(), value.size() + 1);
        }
        *length = static_cast<SQLSMALLINT>(value.size());
        return SQL_SUCCESS;
      },
      0,
      out);
  EXPECT_EQ(rc, SQL_SUCCESS);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(out, value);
}

TEST(UnicodeWrappersTest, FetchNarrowStringPassesMinimumBufferSize) {
  // Imitates SQLGetDiagRec splitting a long message into records of
  // bufferBytes - 1 bytes.
  const std::string message = "0123456789";
  std::string out;
  SQLRETURN rc = fetchNarrowString<SQLSMALLINT>(
      [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
        EXPECT_EQ(bytes, 5);
        const std::string record = message.substr(0, bytes - 1);
        if (buffer) {
          std::memcpy(buffer, record.c_str(), record.size() + 1);
        }
        *length = static_cast<SQLSMALLINT>(record.size());
        return SQL_SUCCESS;
      },
      5,
      out);
  EXPECT_EQ(rc, SQL_SUCCESS);
  EXPECT_EQ(out, "0123");
}

TEST(UnicodeWrappersTest, FetchNarrowStringPropagatesErrors) {
  int calls = 0;
  std::string out;
  SQLRETURN rc = fetchNarrowString<SQLINTEGER>(
      [&](SQLCHAR*, SQLINTEGER, SQLINTEGER*) {
        calls++;
        return SQL_NO_DATA;
      },
      0,
      out);
  EXPECT_EQ(rc, SQL_NO_DATA);
  EXPECT_EQ(calls, 1);
}

TEST(UnicodeWrappersTest, FetchWithBufferGrowsOnce) {
  const std::string value(5000, 'y');
  std::vector<SQLINTEGER> sizes;
  std::string out;
  SQLRETURN rc = fetchNarrowStringWithBuffer<SQLINTEGER>(
      [&](SQLCHAR* buffer, SQLINTEGER bytes, SQLINTEGER* length) {
        sizes.push_back(bytes);
        const std::size_t copy =
            std::min(value.size(), static_cast<std::size_t>(bytes - 1));
        std::memcpy(buffer, value.data(), copy);
        buffer[copy] = 0;
        *length      = static_cast<SQLINTEGER>(value.size());
        return copy < value.size() ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
      },
      4096,
      out);
  EXPECT_EQ(rc, SQL_SUCCESS);
  ASSERT_EQ(sizes.size(), 2u);
  EXPECT_EQ(sizes[0], 4096);
  EXPECT_EQ(sizes[1], 5001);
  EXPECT_EQ(out, value);
}

TEST(UnicodeWrappersTest, NarrowInputKeepsNullArguments) {
  NarrowInput null(nullptr, SQL_NTS, WideTextCodec{});
  EXPECT_EQ(null.data(), nullptr);
  EXPECT_EQ(null.smallLength(), 0);

  const std::vector<SQLWCHAR> text = toSqlWide(CYRILLIC);
  NarrowInput input(text.data(), SQL_NTS, WideTextCodec{});
  ASSERT_NE(input.data(), nullptr);
  EXPECT_EQ(input.utf8(), CYRILLIC);
  EXPECT_EQ(input.smallLength(), 20);
  EXPECT_EQ(input.intLength(), 20);
}

// ---------------------------------------------------------------------------
// W entry points that don't need a server

class UnicodeOfflineHandles : public ::testing::Test {
  protected:
    SQLHENV env = nullptr;
    SQLHDBC dbc = nullptr;

    void SetUp() override {
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env),
                SQL_SUCCESS);
      ASSERT_EQ(SQLSetEnvAttr(
                    env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0),
                SQL_SUCCESS);
      ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc), SQL_SUCCESS);
    }

    void TearDown() override {
      if (dbc) {
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
      }
      if (env) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
      }
    }
};

class DriverUnicodeTypeTest : public UnicodeOfflineHandles {};

TEST_F(DriverUnicodeTypeTest, ReportsSqlWcharEncodingToIodbc) {
  SQLINTEGER value = -1;
  ASSERT_EQ(SQLGetEnvAttr(env, 1065, &value, 0, nullptr), SQL_SUCCESS);
  // SQL_DM_CP_UTF16 = 1, SQL_DM_CP_UCS4 = 3 (iodbcext.h).
  EXPECT_EQ(value, sizeof(SQLWCHAR) == 2 ? 1 : 3);
}

class UnicodeWrappersOfflineTest : public UnicodeOfflineHandles {};

TEST_F(UnicodeWrappersOfflineTest, GetInfoWReturnsBytes) {
  std::vector<SQLWCHAR> buffer(64, 0x7F);
  SQLSMALLINT length = -1;
  SQLRETURN rc =
      SQLGetInfoW(dbc,
                  SQL_DRIVER_NAME,
                  buffer.data(),
                  static_cast<SQLSMALLINT>(buffer.size() * sizeof(SQLWCHAR)),
                  &length);
  ASSERT_EQ(rc, SQL_SUCCESS);
  EXPECT_EQ(fromSqlWide(buffer.data()), "TrinoODBC");
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(9 * sizeof(SQLWCHAR)));

  // Length only.
  length = -1;
  rc     = SQLGetInfoW(dbc, SQL_DRIVER_NAME, nullptr, 0, &length);
  ASSERT_EQ(rc, SQL_SUCCESS);
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(9 * sizeof(SQLWCHAR)));
}

TEST_F(UnicodeWrappersOfflineTest, GetInfoWTruncationIsReportedOnConnection) {
  SQLWCHAR buffer[5] = {};
  SQLSMALLINT length = -1;
  SQLRETURN rc =
      SQLGetInfoW(dbc, SQL_DRIVER_NAME, buffer, sizeof(buffer), &length);
  ASSERT_EQ(rc, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(fromSqlWide(buffer), "Trin");
  EXPECT_EQ(length, static_cast<SQLSMALLINT>(9 * sizeof(SQLWCHAR)));

  SQLWCHAR state[6]         = {};
  SQLWCHAR message[128]     = {};
  SQLSMALLINT messageLength = 0;
  SQLINTEGER native         = 0;
  rc                        = SQLGetDiagRecW(
      SQL_HANDLE_DBC, dbc, 1, state, &native, message, 128, &messageLength);
  ASSERT_EQ(rc, SQL_SUCCESS);
  EXPECT_EQ(fromSqlWide(state), "01004");
  EXPECT_EQ(static_cast<std::size_t>(messageLength),
            fromSqlWide(message).size());
}

TEST_F(UnicodeWrappersOfflineTest, GetDiagRecWTruncatesInCharacters) {
  // Put a long ASCII warning on the connection, then read it back into a
  // small buffer.
  SQLWCHAR info[2] = {};
  ASSERT_EQ(SQLGetInfoW(dbc, SQL_DRIVER_NAME, info, sizeof(info), nullptr),
            SQL_SUCCESS_WITH_INFO);
  SQLWCHAR message[6]       = {};
  SQLSMALLINT messageLength = 0;
  SQLRETURN rc              = SQLGetDiagRecW(
      SQL_HANDLE_DBC, dbc, 1, nullptr, nullptr, message, 6, &messageLength);
  ASSERT_EQ(rc, SQL_SUCCESS_WITH_INFO);
  EXPECT_EQ(fromSqlWide(message), "Strin");
  EXPECT_EQ(messageLength,
            static_cast<SQLSMALLINT>(
                std::string("String data, right truncated").size()));
}

TEST_F(UnicodeWrappersOfflineTest, NumericInfoPassesThrough) {
  SQLUSMALLINT value = 0xFFFF;
  ASSERT_EQ(SQLGetInfoW(dbc, SQL_MAX_CONCURRENT_ACTIVITIES, &value, 0, nullptr),
            SQL_SUCCESS);
  EXPECT_EQ(value, 0);
}

// ---------------------------------------------------------------------------

TEST(StringAttributesTest, ClassifiesStringValues) {
  EXPECT_TRUE(isStringInfoType(SQL_DRIVER_NAME));
  EXPECT_TRUE(isStringInfoType(SQL_DBMS_VER));
  EXPECT_TRUE(isStringInfoType(SQL_IDENTIFIER_QUOTE_CHAR));
  EXPECT_FALSE(isStringInfoType(SQL_MAX_CONCURRENT_ACTIVITIES));
  EXPECT_FALSE(isStringInfoType(SQL_GETDATA_EXTENSIONS));

  EXPECT_TRUE(isStringConnectAttribute(SQL_ATTR_CURRENT_CATALOG));
  EXPECT_FALSE(isStringConnectAttribute(SQL_ATTR_AUTOCOMMIT));
  EXPECT_FALSE(isStringConnectAttribute(SQL_ATTR_CONNECTION_DEAD));

  EXPECT_TRUE(isStringDescriptorField(SQL_DESC_NAME));
  EXPECT_TRUE(isStringDescriptorField(SQL_DESC_TYPE_NAME));
  EXPECT_TRUE(isStringDescriptorField(SQL_DESC_LABEL));
  EXPECT_FALSE(isStringDescriptorField(SQL_DESC_CONCISE_TYPE));
  EXPECT_FALSE(isStringDescriptorField(SQL_DESC_OCTET_LENGTH));
}

// ---------------------------------------------------------------------------
// Widened narrow text (WideTextCodec): an ANSI application's A calls that
// iODBC turned into W calls byte by byte.
// ---------------------------------------------------------------------------

TEST(WideTextTest, Utf8ConnectionsUseUnicode) {
  EXPECT_FALSE(wideTextCodecFor(utf8AnsiEncoding()).widenedAnsi.has_value());
}

#ifdef __APPLE__
namespace {

// "Привет" in Mac Cyrillic, widened byte by byte.
const std::u32string PRIVET_WIDENED = U"\u008Fðèâåò";
const std::string PRIVET_UTF8 =
    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";

AnsiEncoding macCyrillic() {
  return resolveAnsiEncoding("x-mac-cyrillic");
}

} // namespace

TEST(WideTextTest, LegacyEncodingsAreWidenedWithIodbc) {
  // iODBC's SQLWCHAR is 4 bytes.
  const WideTextCodec codec = wideTextCodecFor(macCyrillic());
  ASSERT_TRUE(codec.widenedAnsi.has_value());
  EXPECT_EQ(*codec.widenedAnsi, macCyrillic());
}

TEST(WideTextTest, WidenedInputIsDecodedInTheEncoding) {
  EXPECT_EQ(widenedAnsiToUtf8<char32_t>(PRIVET_WIDENED, macCyrillic()),
            PRIVET_UTF8);
  const std::u16string privet16(PRIVET_WIDENED.begin(), PRIVET_WIDENED.end());
  EXPECT_EQ(widenedAnsiToUtf8<char16_t>(privet16, macCyrillic()), PRIVET_UTF8);
  EXPECT_EQ(widenedAnsiToUtf8<char32_t>(U"WHERE x = '" + PRIVET_WIDENED + U"'",
                                        macCyrillic()),
            "WHERE x = '" + PRIVET_UTF8 + "'");
}

TEST(WideTextTest, OtherInputIsReadAsUnicode) {
  // Plain ASCII: the same either way.
  EXPECT_EQ(widenedAnsiToUtf8<char32_t>(U"SELECT 1", macCyrillic()),
            std::nullopt);
  // Real Unicode text (a W application): something above U+00FF.
  EXPECT_EQ(widenedAnsiToUtf8<char32_t>(U"Пé", macCyrillic()), std::nullopt);
  EXPECT_EQ(widenedAnsiToUtf8<char32_t>(U"", macCyrillic()), std::nullopt);
}

TEST(WideTextTest, SqlWideInputWithCodec) {
  const WideTextCodec codec = wideTextCodecFor(macCyrillic());
  std::vector<SQLWCHAR> widened(PRIVET_WIDENED.begin(), PRIVET_WIDENED.end());
  widened.push_back(0);
  EXPECT_EQ(sqlWideToUtf8(widened.data(), SQL_NTS, codec), PRIVET_UTF8);
  EXPECT_EQ(sqlWideToUtf8(widened.data(), 1, codec), "\xD0\x9F"); // П
  // Without the codec the bytes are Latin-1 characters.
  EXPECT_NE(sqlWideToUtf8(widened.data(), SQL_NTS, WideTextCodec{}),
            PRIVET_UTF8);

  const std::vector<SQLWCHAR> unicode = {0x41F, 0x440, 0};
  EXPECT_EQ(sqlWideToUtf8(unicode.data(), SQL_NTS, codec), "\xD0\x9F\xD1\x80");
}

TEST(WideTextTest, OutputIsWidenedInTheEncoding) {
  EXPECT_EQ(utf8ToWidenedAnsi<char32_t>(PRIVET_UTF8, macCyrillic()),
            PRIVET_WIDENED);
  EXPECT_EQ(utf8ToWidenedAnsi<char32_t>("id", macCyrillic()), U"id");
  // What the encoding can't represent becomes '?' (one per UTF-16 unit, as
  // in SQL_C_CHAR data).
  EXPECT_EQ(utf8ToWidenedAnsi<char32_t>("\xF0\x9F\x98\x80", macCyrillic()),
            U"??");
  // Invalid UTF-8 doesn't throw.
  EXPECT_EQ(utf8ToWidenedAnsi<char32_t>("a\xFF", macCyrillic()), U"a?");
  // GB18030: one unit per byte, 4 for the emoji.
  EXPECT_EQ(utf8ToWidenedAnsi<char32_t>("\xF0\x9F\x98\x80",
                                        resolveAnsiEncoding("GB18030")),
            U"\u00949ü6");
}

TEST(WideTextTest, WriteWideOutputWithCodec) {
  const WideTextCodec codec = wideTextCodecFor(macCyrillic());
  std::vector<SQLWCHAR> buffer(16, 0);
  SQLSMALLINT length = 0;
  EXPECT_FALSE(writeWideOutput<SQLSMALLINT>(PRIVET_UTF8,
                                            buffer.data(),
                                            static_cast<SQLSMALLINT>(16),
                                            &length,
                                            WideLengthUnit::Characters,
                                            codec));
  EXPECT_EQ(length, 6);
  EXPECT_EQ(std::u32string(buffer.begin(), buffer.begin() + 6), PRIVET_WIDENED);
}
TEST(WideTextTest, MacHebrewInputHasNoBidiMarks) {
  // Core Foundation adds directional marks around ASCII it decodes from Mac
  // Hebrew; they would break the SQL.
  const std::string sql = "SELECT '\xF9\xEC\xE5\xED' AS x";
  EXPECT_EQ(ansiToUtf8(sql, resolveAnsiEncoding("x-mac-hebrew")),
            "SELECT '\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D' AS x");
  std::u32string units;
  for (const char c : sql) {
    units.push_back(static_cast<unsigned char>(c));
  }
  EXPECT_EQ(
      widenedAnsiToUtf8<char32_t>(units, resolveAnsiEncoding("x-mac-hebrew")),
      "SELECT '\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D' AS x");
}

TEST(WideTextTest, WidenedOutputIsCutBetweenNarrowCharacters) {
  const AnsiEncoding gb18030 = resolveAnsiEncoding("GB18030");
  const WideTextCodec codec  = wideTextCodecFor(gb18030);
  // "a" + U+1F600 (4 bytes in GB18030), widened.
  const SqlWideString text = utf8ToSqlWide("a\xF0\x9F\x98\x80", codec);
  ASSERT_EQ(text.size(), 5U);
  std::vector<SQLWCHAR> buffer(8, 0x7777);
  // Room for 3 units plus the terminator: only "a" fits whole.
  WideCopyResult copy = copyToSqlWideBuffer(text, buffer.data(), 4, codec);
  EXPECT_TRUE(copy.truncated);
  EXPECT_EQ(copy.totalUnits, 5U);
  EXPECT_EQ(buffer[0], static_cast<SQLWCHAR>('a'));
  EXPECT_EQ(buffer[1], static_cast<SQLWCHAR>(0));
  copy = copyToSqlWideBuffer(text, buffer.data(), 6, codec);
  EXPECT_FALSE(copy.truncated);
  EXPECT_EQ(buffer[5], static_cast<SQLWCHAR>(0));
}
#endif
