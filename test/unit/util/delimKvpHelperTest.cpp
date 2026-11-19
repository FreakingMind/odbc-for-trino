#include <gtest/gtest.h>

#include <map>
#include <string>

#include "../../../src/util/delimKvpHelper.hpp"

TEST(DelimKvpHelperTest, ParsesConnectionString) {
  auto kvps = parseKVPsFromSemicolonDelimStr("DSN=Trino; UID=alice;PWD=x;");
  EXPECT_EQ(kvps.at("dsn"), "Trino");
  EXPECT_EQ(kvps.at("uid"), "alice");
  EXPECT_EQ(kvps.at("pwd"), "x");
}

TEST(DelimKvpHelperTest, BracedValuesMayContainDelimiters) {
  auto kvps = parseKVPsFromSemicolonDelimStr(
      "DSN=Trino;PWD={a;b=c}}};UID={alice};catalog=iceberg");
  EXPECT_EQ(kvps.at("pwd"), "a;b=c}");
  EXPECT_EQ(kvps.at("uid"), "alice");
  EXPECT_EQ(kvps.at("catalog"), "iceberg");
}

TEST(DelimKvpHelperTest, EmptyBracesAndLastValue) {
  auto kvps = parseKVPsFromSemicolonDelimStr("UID={};PWD={x}}}");
  EXPECT_EQ(kvps.at("uid"), "");
  EXPECT_EQ(kvps.at("pwd"), "x}");
}

TEST(DelimKvpHelperTest, UnclosedBraceIsKeptLiterally) {
  auto kvps = parseKVPsFromSemicolonDelimStr("PWD={abc;UID=bob");
  EXPECT_EQ(kvps.at("pwd"), "{abc");
  EXPECT_EQ(kvps.at("uid"), "bob");
}

TEST(DelimKvpHelperTest, CommaListDoesNotUseBraces) {
  // The WWW-Authenticate header format is unchanged.
  auto kvps = parseKVPsFromCommaDelimStr(R"(x_redirect_server="{a}", b="c")");
  EXPECT_EQ(kvps.at("x_redirect_server"), "{a}");
  EXPECT_EQ(kvps.at("b"), "c");
}
