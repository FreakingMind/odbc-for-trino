#include "../../src/util/windowsLean.hpp"

#include <gtest/gtest.h>
#include <sql.h>
#include <sqlext.h>
#include <string>

#include "../../src/driver/execute.hpp"

// ---------------------------------------------------------------------------
// Helper: build a minimal DescriptorField for a given C type / buffer pointer.
// ---------------------------------------------------------------------------
static DescriptorField makeField(SQLSMALLINT cType, void* buf) {
  DescriptorField f;
  f.bufferCDataType = cType;
  f.bufferPtr       = buf;
  return f;
}

// ---------------------------------------------------------------------------
// SQL_C_CHAR – plain strings
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, CharSimpleString) {
  char value[]      = "hello";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "'hello'");
}

TEST(DescriptorFieldToParameterTextTest, CharEmptyString) {
  char value[]      = "";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "''");
}

// ---------------------------------------------------------------------------
// SQL_C_CHAR – SQL injection: embedded single quotes must be doubled
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, CharEscapesSingleQuote) {
  char value[]      = "O'Brien";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "'O''Brien'");
}

TEST(DescriptorFieldToParameterTextTest, CharEscapesMultipleSingleQuotes) {
  char value[]      = "it's a 'test'";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "'it''s a ''test'''");
}

TEST(DescriptorFieldToParameterTextTest, CharSqlInjectionAttempt) {
  // Classic injection: closing the string early then appending SQL
  char value[]      = "'; DROP TABLE users; --";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "'''; DROP TABLE users; --'");
}

// ---------------------------------------------------------------------------
// SQL_C_LONG – 32-bit signed integer
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, LongPositive) {
  SQLINTEGER value  = 42;
  DescriptorField f = makeField(SQL_C_LONG, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "42");
}

TEST(DescriptorFieldToParameterTextTest, LongNegative) {
  SQLINTEGER value  = -7;
  DescriptorField f = makeField(SQL_C_LONG, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "-7");
}

TEST(DescriptorFieldToParameterTextTest, LongZero) {
  SQLINTEGER value  = 0;
  DescriptorField f = makeField(SQL_C_LONG, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "0");
}

// ---------------------------------------------------------------------------
// SQL_C_FLOAT – single-precision floating point
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, FloatPositive) {
  float value       = 3.14f;
  DescriptorField f = makeField(SQL_C_FLOAT, &value);
  // std::format renders float with enough precision to round-trip; just
  // verify the returned string converts back to the same value.
  const std::string result =
      descriptorFieldToParameterText(f, utf8AnsiEncoding());
  EXPECT_FLOAT_EQ(std::stof(result), value);
}

TEST(DescriptorFieldToParameterTextTest, FloatNegative) {
  float value       = -1.5f;
  DescriptorField f = makeField(SQL_C_FLOAT, &value);
  const std::string result =
      descriptorFieldToParameterText(f, utf8AnsiEncoding());
  EXPECT_FLOAT_EQ(std::stof(result), value);
}

TEST(DescriptorFieldToParameterTextTest, FloatZero) {
  float value       = 0.0f;
  DescriptorField f = makeField(SQL_C_FLOAT, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()), "0");
}

// ---------------------------------------------------------------------------
// SQL_C_TIME
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, Time) {
  SQL_TIME_STRUCT value = {1, 2, 3}; // 01:02:03
  DescriptorField f     = makeField(SQL_C_TIME, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "TIME '01:02:03'");
}

// ---------------------------------------------------------------------------
// SQL_C_DATE
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, Date) {
  SQL_DATE_STRUCT value = {2001, 8, 22}; // 2001-08-22
  DescriptorField f     = makeField(SQL_C_DATE, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "DATE '2001-08-22'");
}

// ---------------------------------------------------------------------------
// SQL_C_TIMESTAMP – without and with fractional seconds
// ---------------------------------------------------------------------------
TEST(DescriptorFieldToParameterTextTest, TimestampNoFraction) {
  SQL_TIMESTAMP_STRUCT value = {2020, 6, 10, 15, 55, 23, 0}; // no sub-second
  DescriptorField f          = makeField(SQL_C_TIMESTAMP, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "TIMESTAMP '2020-06-10 15:55:23'");
}

TEST(DescriptorFieldToParameterTextTest, TimestampWithFraction) {
  SQL_TIMESTAMP_STRUCT value = {
      2020, 6, 10, 15, 55, 23, 500000000}; // .5 seconds
  DescriptorField f = makeField(SQL_C_TIMESTAMP, &value);
  EXPECT_EQ(descriptorFieldToParameterText(f, utf8AnsiEncoding()),
            "TIMESTAMP '2020-06-10 15:55:23.500000000'");
}

#ifdef __APPLE__
TEST(DescriptorFieldToParameterTextTest, CharIsReadInTheAnsiEncoding) {
  // "Привет" in Mac Cyrillic, as Excel sends it with a Russian UI.
  char value[]      = "\x8F\xF0\xE8\xE2\xE5\xF2";
  DescriptorField f = makeField(SQL_C_CHAR, value);
  EXPECT_EQ(
      descriptorFieldToParameterText(f, resolveAnsiEncoding("x-mac-cyrillic")),
      "'\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82'");
  // Bytes the encoding can't decode pass through.
  char invalid[]    = "\x81";
  DescriptorField g = makeField(SQL_C_CHAR, invalid);
  EXPECT_EQ(descriptorFieldToParameterText(g, resolveAnsiEncoding("Shift_JIS")),
            "'\x81'");
}
#endif
