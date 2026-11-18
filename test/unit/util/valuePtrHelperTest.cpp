#include "gtest/gtest.h"
#include <string>

#include "../../../src/util/valuePtrHelper.hpp"

TEST(ValuePtrHelperTest, CanWriteNullTermStringToPtr) {
  // Setup
  std::string s   = "hello";
  char buffer[20] = {};
  short len       = 0;

  // Test
  bool truncated = writeNullTermStringToPtr(buffer, s, sizeof(buffer), &len);

  // Assert
  EXPECT_FALSE(truncated);
  EXPECT_STREQ(s.c_str(), buffer);
  EXPECT_EQ(len, s.length());
}

TEST(ValuePtrHelperTest, CanWriteEmptyStringToPtr) {
  // Setup
  std::string s  = "";
  char buffer[5] = {'x', 'x', 'x', 'x', 'x'};
  short len      = 7;

  // Test
  bool truncated = writeNullTermStringToPtr(buffer, s, sizeof(buffer), &len);

  // Assert
  EXPECT_FALSE(truncated);
  EXPECT_STREQ(s.c_str(), buffer);
  EXPECT_EQ(len, 0);
}

TEST(ValuePtrHelperTest, NullCharInCorrectPosition) {
  /*
  Buffer: "bbbbbbbb"
  String: "aaaa"
  Expected Result: "aaaa\0bbb" (nothing written past the terminator)
  */
  std::string s  = "aaaa";
  char buffer[9] = {"bbbbbbbb"};
  short len      = 0;

  // Test
  bool truncated = writeNullTermStringToPtr(buffer, s, sizeof(buffer), &len);

  // Assert
  EXPECT_FALSE(truncated);
  EXPECT_STREQ(s.c_str(), buffer); // Correct data
  EXPECT_EQ(len, s.length());      // Correct length
  EXPECT_EQ(buffer[4], '\0');      // Check for null terminator
  EXPECT_EQ(buffer[5], 'b');       // Check for correct padding
  EXPECT_EQ(buffer[6], 'b');       // Check for correct padding
  EXPECT_EQ(buffer[7], 'b');       // Check for correct padding
  EXPECT_EQ(buffer[8], '\0');      // Check for original null terminator.
}

TEST(ValuePtrHelperTest, TruncatesToBufferLength) {
  std::string s  = "localhost.example";
  char buffer[8] = {"zzzzzzz"};
  short len      = 0;

  bool truncated = writeNullTermStringToPtr(buffer, s, 5, &len);

  EXPECT_TRUE(truncated);
  EXPECT_STREQ(buffer, "loca");
  // The full length is reported so the caller can retry.
  EXPECT_EQ(len, s.length());
  // Nothing past BufferLength was written.
  EXPECT_EQ(buffer[5], 'z');
}

TEST(ValuePtrHelperTest, ExactFitNeedsRoomForTerminator) {
  std::string s  = "abcd";
  char buffer[4] = {};
  SQLINTEGER len = 0;

  bool truncated = writeNullTermStringToPtr(buffer, s, sizeof(buffer), &len);

  EXPECT_TRUE(truncated);
  EXPECT_STREQ(buffer, "abc");
  EXPECT_EQ(len, 4);
}

TEST(ValuePtrHelperTest, DoesNotSplitUtf8Characters) {
  // "Привет" is 12 bytes; a 6-byte buffer has room for 5 bytes of payload,
  // which would end in the middle of the third letter.
  std::string s  = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
  char buffer[6] = {};
  short len      = 0;

  bool truncated = writeNullTermStringToPtr(buffer, s, sizeof(buffer), &len);

  EXPECT_TRUE(truncated);
  EXPECT_EQ(std::string(buffer), "\xD0\x9F\xD1\x80");
  EXPECT_EQ(len, 12);
}

TEST(ValuePtrHelperTest, NullBufferOnlyReportsLength) {
  std::string s = "system";
  short len     = 0;

  bool truncated = writeNullTermStringToPtr(nullptr, s, 0, &len);

  EXPECT_FALSE(truncated);
  EXPECT_EQ(len, 6);
}

TEST(ValuePtrHelperTest, ZeroLengthBufferIsNotWritten) {
  std::string s  = "abc";
  char buffer[2] = {'x', 'x'};
  short len      = 0;

  bool truncated = writeNullTermStringToPtr(buffer, s, 0, &len);

  EXPECT_TRUE(truncated);
  EXPECT_EQ(buffer[0], 'x');
  EXPECT_EQ(len, 3);
}
