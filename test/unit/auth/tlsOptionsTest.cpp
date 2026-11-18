#include <gtest/gtest.h>
#include <stdexcept>

#include "../../../src/trinoAPIWrapper/tlsOptions.hpp"

TEST(TlsOptionsTest, ParsesValuesCaseInsensitively) {
  EXPECT_EQ(parseSslVerification("FULL"), SslVerification::Full);
  EXPECT_EQ(parseSslVerification("full"), SslVerification::Full);
  EXPECT_EQ(parseSslVerification("Ca"), SslVerification::Ca);
  EXPECT_EQ(parseSslVerification("none"), SslVerification::None);
}

TEST(TlsOptionsTest, EmptyMeansFull) {
  EXPECT_EQ(parseSslVerification(""), SslVerification::Full);
}

TEST(TlsOptionsTest, RejectsUnknownValues) {
  EXPECT_THROW(parseSslVerification("off"), std::invalid_argument);
  EXPECT_THROW(parseSslVerification("true"), std::invalid_argument);
}

TEST(TlsOptionsTest, RoundTripsThroughToString) {
  for (auto v :
       {SslVerification::Full, SslVerification::Ca, SslVerification::None}) {
    EXPECT_EQ(parseSslVerification(toString(v)), v);
  }
}
