#include "gtest/gtest.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

#include "../../../src/util/rowToBuffer.hpp"

using json = nlohmann::json;

namespace {
// Converts the single value of a one-column row.
template <typename T>
ColumnToBufferStatus
convert(const json& value, SQLSMALLINT cType, T& out, SQLLEN* ind = nullptr) {
  static SQLLEN unusedInd = 0;
  return columnToBuffer(cType,
                        SQL_UNKNOWN_TYPE,
                        json::array({value}),
                        1,
                        &out,
                        sizeof(T),
                        ind ? ind : &unusedInd,
                        0,
                        0,
                        utf8AnsiEncoding());
}

std::string convertToChar(const json& value) {
  char buffer[64]             = {};
  SQLLEN ind                  = 0;
  ColumnToBufferStatus status = columnToBuffer(SQL_C_CHAR,
                                               SQL_UNKNOWN_TYPE,
                                               json::array({value}),
                                               1,
                                               buffer,
                                               sizeof(buffer),
                                               &ind,
                                               0,
                                               0,
                                               utf8AnsiEncoding());
  EXPECT_TRUE(status.isSuccess) << status.sqlState;
  EXPECT_EQ(ind, static_cast<SQLLEN>(std::strlen(buffer)));
  return buffer;
}
} // namespace

TEST(RowToBufferTest, CharFromEveryJsonType) {
  EXPECT_EQ(convertToChar("text"), "text");
  EXPECT_EQ(convertToChar(42), "42");
  EXPECT_EQ(convertToChar(-42), "-42");
  EXPECT_EQ(convertToChar(true), "1");
  EXPECT_EQ(convertToChar(false), "0");
  EXPECT_EQ(convertToChar(1.5), "1.5");
  EXPECT_EQ(convertToChar(json::array({1, 2})), "[1,2]");
  EXPECT_EQ(convertToChar(json::object({{"k", "v"}})), "{\"k\":\"v\"}");
}

TEST(RowToBufferTest, DoubleSpecialValues) {
  double d = 0;
  ASSERT_TRUE(convert(json("NaN"), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_TRUE(std::isnan(d));
  ASSERT_TRUE(convert(json("Infinity"), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_EQ(d, std::numeric_limits<double>::infinity());
  ASSERT_TRUE(convert(json("-Infinity"), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_EQ(d, -std::numeric_limits<double>::infinity());

  float f = 0;
  ASSERT_TRUE(convert(json("NaN"), SQL_C_FLOAT, f).isSuccess);
  EXPECT_TRUE(std::isnan(f));
  ASSERT_TRUE(convert(json("-Infinity"), SQL_C_FLOAT, f).isSuccess);
  EXPECT_EQ(f, -std::numeric_limits<float>::infinity());
}

TEST(RowToBufferTest, DoubleFromNumbersAndText) {
  double d   = 0;
  SQLLEN ind = 0;
  ASSERT_TRUE(convert(json(2.25), SQL_C_DOUBLE, d, &ind).isSuccess);
  EXPECT_EQ(d, 2.25);
  EXPECT_EQ(ind, static_cast<SQLLEN>(sizeof(double)));
  ASSERT_TRUE(convert(json(7), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_EQ(d, 7.0);
  // DECIMAL values arrive as strings.
  ASSERT_TRUE(convert(json("-12.50"), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_EQ(d, -12.5);
  ASSERT_TRUE(convert(json("1e3"), SQL_C_DOUBLE, d).isSuccess);
  EXPECT_EQ(d, 1000.0);
}

TEST(RowToBufferTest, InvalidTextIsInvalidCast) {
  double d = 0;
  for (const char* text : {"abc", "", "1.5x", "nan"}) {
    ColumnToBufferStatus status = convert(json(text), SQL_C_DOUBLE, d);
    EXPECT_FALSE(status.isSuccess) << text;
    EXPECT_EQ(status.sqlState, "22018") << text;
  }
  SQLINTEGER i                = 0;
  ColumnToBufferStatus status = convert(json("12a"), SQL_C_SLONG, i);
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "22018");
}

TEST(RowToBufferTest, StructuredValuesToNumbersAreRestricted) {
  SQLINTEGER i                = 0;
  ColumnToBufferStatus status = convert(json::array({1}), SQL_C_SLONG, i);
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "07006");
}

TEST(RowToBufferTest, IntegerRangeChecks) {
  signed char tiny = 0;
  EXPECT_TRUE(convert(json(127), SQL_C_STINYINT, tiny).isSuccess);
  EXPECT_EQ(tiny, 127);
  ColumnToBufferStatus status = convert(json(128), SQL_C_STINYINT, tiny);
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "22003");

  unsigned short ushort = 0;
  EXPECT_EQ(convert(json(-1), SQL_C_USHORT, ushort).sqlState, "22003");
  EXPECT_TRUE(convert(json(65535), SQL_C_USHORT, ushort).isSuccess);
  EXPECT_EQ(ushort, 65535);

  SQLINTEGER i = 0;
  EXPECT_EQ(convert(json(int64_t{1} << 40), SQL_C_SLONG, i).sqlState, "22003");
  EXPECT_EQ(convert(json(1e20), SQL_C_SLONG, i).sqlState, "22003");

  SQLBIGINT big = 0;
  EXPECT_TRUE(
      convert(json("-9223372036854775808"), SQL_C_SBIGINT, big).isSuccess);
  EXPECT_EQ(big, std::numeric_limits<SQLBIGINT>::min());
  EXPECT_EQ(convert(json("9223372036854775808"), SQL_C_SBIGINT, big).sqlState,
            "22003");

  SQLUBIGINT ubig = 0;
  EXPECT_TRUE(
      convert(json(uint64_t{18446744073709551615u}), SQL_C_UBIGINT, ubig)
          .isSuccess);
  EXPECT_EQ(ubig, std::numeric_limits<SQLUBIGINT>::max());
}

TEST(RowToBufferTest, IntegersFromDecimalTextAndBooleans) {
  SQLINTEGER i = 0;
  // The fractional part is truncated.
  ASSERT_TRUE(convert(json("123.99"), SQL_C_SLONG, i).isSuccess);
  EXPECT_EQ(i, 123);
  ASSERT_TRUE(convert(json("-5.5"), SQL_C_SLONG, i).isSuccess);
  EXPECT_EQ(i, -5);
  ASSERT_TRUE(convert(json(9.9), SQL_C_SLONG, i).isSuccess);
  EXPECT_EQ(i, 9);
  ASSERT_TRUE(convert(json(true), SQL_C_SLONG, i).isSuccess);
  EXPECT_EQ(i, 1);
}

TEST(RowToBufferTest, Bit) {
  unsigned char bit = 9;
  ASSERT_TRUE(convert(json(true), SQL_C_BIT, bit).isSuccess);
  EXPECT_EQ(bit, 1);
  ASSERT_TRUE(convert(json(false), SQL_C_BIT, bit).isSuccess);
  EXPECT_EQ(bit, 0);
  ASSERT_TRUE(convert(json(1), SQL_C_BIT, bit).isSuccess);
  EXPECT_EQ(bit, 1);
  EXPECT_EQ(convert(json(2), SQL_C_BIT, bit).sqlState, "22003");
}

TEST(RowToBufferTest, NullNeedsIndicator) {
  SQLINTEGER i                = 0;
  SQLLEN ind                  = 0;
  ColumnToBufferStatus status = columnToBuffer(SQL_C_SLONG,
                                               SQL_INTEGER,
                                               json::array({nullptr}),
                                               1,
                                               &i,
                                               0,
                                               &ind,
                                               0,
                                               0,
                                               utf8AnsiEncoding());
  EXPECT_TRUE(status.isSuccess);
  EXPECT_EQ(ind, SQL_NULL_DATA);

  status = columnToBuffer(SQL_C_SLONG,
                          SQL_INTEGER,
                          json::array({nullptr}),
                          1,
                          &i,
                          0,
                          nullptr,
                          0,
                          0,
                          utf8AnsiEncoding());
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "22002");
}

TEST(RowToBufferTest, FixedTypeNeedsBuffer) {
  SQLLEN ind                  = 0;
  ColumnToBufferStatus status = columnToBuffer(SQL_C_SLONG,
                                               SQL_INTEGER,
                                               json::array({1}),
                                               1,
                                               nullptr,
                                               0,
                                               &ind,
                                               0,
                                               0,
                                               utf8AnsiEncoding());
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "HY009");
}

TEST(RowToBufferTest, ColumnOutOfRange) {
  SQLINTEGER i                = 0;
  SQLLEN ind                  = 0;
  ColumnToBufferStatus status = columnToBuffer(SQL_C_SLONG,
                                               SQL_INTEGER,
                                               json::array({1}),
                                               2,
                                               &i,
                                               0,
                                               &ind,
                                               0,
                                               0,
                                               utf8AnsiEncoding());
  EXPECT_FALSE(status.isSuccess);
  EXPECT_EQ(status.sqlState, "07009");
}

TEST(RowToBufferTest, Numeric) {
  SQL_NUMERIC_STRUCT numeric{};
  ASSERT_TRUE(convert(json("-1.25"), SQL_C_NUMERIC, numeric).isSuccess);
  EXPECT_EQ(numeric.sign, 0);
  EXPECT_EQ(numeric.scale, 2);
  EXPECT_EQ(numeric.val[0], 125);
  EXPECT_EQ(numeric.val[1], 0);

  ASSERT_TRUE(convert(json(258), SQL_C_NUMERIC, numeric).isSuccess);
  EXPECT_EQ(numeric.sign, 1);
  EXPECT_EQ(numeric.scale, 0);
  EXPECT_EQ(numeric.val[0], 2);
  EXPECT_EQ(numeric.val[1], 1);

  EXPECT_EQ(convert(json("12x"), SQL_C_NUMERIC, numeric).sqlState, "22018");
  // More digits than the encoder handles: out of range, not a crash.
  EXPECT_EQ(
      convert(json("123456789012345678901234567890"), SQL_C_NUMERIC, numeric)
          .sqlState,
      "22003");
}

TEST(RowToBufferTest, GuidAndDates) {
  SQLGUID guid{};
  ASSERT_TRUE(
      convert(json("12345678-9abc-def0-1234-56789abcdef0"), SQL_C_GUID, guid)
          .isSuccess);
  EXPECT_EQ(guid.Data1, 0x12345678u);
  EXPECT_EQ(guid.Data2, 0x9abc);
  EXPECT_EQ(guid.Data3, 0xdef0);
  EXPECT_EQ(guid.Data4[0], 0x12);
  EXPECT_EQ(guid.Data4[7], 0xf0);
  EXPECT_EQ(convert(json("not-a-guid"), SQL_C_GUID, guid).sqlState, "22018");

  SQL_DATE_STRUCT date{};
  ASSERT_TRUE(convert(json("2024-02-29"), SQL_C_TYPE_DATE, date).isSuccess);
  EXPECT_EQ(date.year, 2024);
  EXPECT_EQ(date.month, 2);
  EXPECT_EQ(date.day, 29);
  // Short strings are rejected instead of being read past their end.
  EXPECT_EQ(convert(json("2024"), SQL_C_TYPE_DATE, date).sqlState, "22018");
  EXPECT_EQ(convert(json(5), SQL_C_TYPE_DATE, date).sqlState, "07006");
}

// ODBC allows converting between the date/time types: Excel for Mac binds
// DATE columns as SQL_C_TYPE_TIMESTAMP, for example.
TEST(RowToBufferTest, DateToTimestamp) {
  SQL_TIMESTAMP_STRUCT ts{};
  ASSERT_TRUE(convert(json("2024-01-15"), SQL_C_TYPE_TIMESTAMP, ts).isSuccess);
  EXPECT_EQ(ts.year, 2024);
  EXPECT_EQ(ts.month, 1);
  EXPECT_EQ(ts.day, 15);
  EXPECT_EQ(ts.hour, 0);
  EXPECT_EQ(ts.minute, 0);
  EXPECT_EQ(ts.second, 0);
  EXPECT_EQ(ts.fraction, 0U);
  ASSERT_TRUE(convert(json("1999-12-31"), SQL_C_TIMESTAMP, ts).isSuccess);
  EXPECT_EQ(ts.year, 1999);
  EXPECT_EQ(ts.day, 31);
}

TEST(RowToBufferTest, TimeToTimestampUsesTodaysDate) {
  SQL_TIMESTAMP_STRUCT ts{};
  ASSERT_TRUE(
      convert(json("10:20:30.250"), SQL_C_TYPE_TIMESTAMP, ts).isSuccess);
  EXPECT_GE(ts.year, 2024);
  EXPECT_EQ(ts.hour, 10);
  EXPECT_EQ(ts.minute, 20);
  EXPECT_EQ(ts.second, 30);
  // ODBC: no fractional seconds when converting a time.
  EXPECT_EQ(ts.fraction, 0U);
}

TEST(RowToBufferTest, TimestampToDateAndTime) {
  SQL_DATE_STRUCT date{};
  ASSERT_TRUE(convert(json("2024-01-15 10:20:30.123"), SQL_C_TYPE_DATE, date)
                  .isSuccess);
  EXPECT_EQ(date.year, 2024);
  EXPECT_EQ(date.month, 1);
  EXPECT_EQ(date.day, 15);

  SQL_TIME_STRUCT time{};
  ASSERT_TRUE(convert(json("2024-01-15 10:20:30.123"), SQL_C_TYPE_TIME, time)
                  .isSuccess);
  EXPECT_EQ(time.hour, 10);
  EXPECT_EQ(time.minute, 20);
  EXPECT_EQ(time.second, 30);

  // With a time zone: the wall-clock date and time, like CAST in Trino.
  for (const char* zoned : {"2024-01-15 01:20:30.000 Europe/Moscow",
                            "2024-01-15 01:20:30.000 +03:00"}) {
    ASSERT_TRUE(convert(json(zoned), SQL_C_TYPE_DATE, date).isSuccess) << zoned;
    EXPECT_EQ(date.day, 15) << zoned;
    ASSERT_TRUE(convert(json(zoned), SQL_C_TYPE_TIME, time).isSuccess) << zoned;
    EXPECT_EQ(time.hour, 1) << zoned;
  }
}

TEST(RowToBufferTest, ZonedTimestampsWithOffsetsAndDstEdges) {
  SQL_TIMESTAMP_STRUCT ts{};
  // Offset zones, as Trino prints them, are converted to UTC.
  ASSERT_TRUE(
      convert(json("2024-01-15 10:20:30.123 +03:00"), SQL_C_TYPE_TIMESTAMP, ts)
          .isSuccess);
  EXPECT_EQ(ts.hour, 7);
  EXPECT_EQ(ts.fraction, 123000000U);
  ASSERT_TRUE(
      convert(json("2024-01-15 23:30:00 -05:30"), SQL_C_TYPE_TIMESTAMP, ts)
          .isSuccess);
  EXPECT_EQ(ts.day, 16);
  EXPECT_EQ(ts.hour, 5);
  EXPECT_EQ(ts.minute, 0);
  // A wall-clock time repeated by the end of DST (02:30 happens twice in
  // Berlin on 2024-10-27): the earlier instant, 00:30 UTC.
  ASSERT_TRUE(convert(json("2024-10-27 02:30:00 Europe/Berlin"),
                      SQL_C_TYPE_TIMESTAMP,
                      ts)
                  .isSuccess);
  EXPECT_EQ(ts.hour, 0);
  EXPECT_EQ(ts.minute, 30);
  // A wall-clock time skipped by the start of DST still converts.
  EXPECT_TRUE(convert(json("2024-03-31 02:30:00 Europe/Berlin"),
                      SQL_C_TYPE_TIMESTAMP,
                      ts)
                  .isSuccess);
}

TEST(RowToBufferTest, DateTimeMismatchesAreRejected) {
  SQL_DATE_STRUCT date{};
  EXPECT_EQ(convert(json("10:20:30"), SQL_C_TYPE_DATE, date).sqlState, "22018");
  SQL_TIME_STRUCT time{};
  EXPECT_EQ(convert(json("2024-01-15"), SQL_C_TYPE_TIME, time).sqlState,
            "22018");
  SQL_TIMESTAMP_STRUCT ts{};
  EXPECT_EQ(convert(json("garbage"), SQL_C_TYPE_TIMESTAMP, ts).sqlState,
            "22018");
}
