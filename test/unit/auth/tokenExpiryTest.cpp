#include <gtest/gtest.h>

#include "../../../src/trinoAPIWrapper/authProvider/tokens/tokenCache.hpp"

namespace {
constexpr long long NOW = 1'800'000'000;
} // namespace

TEST(TokenExpiryTest, MissingExpiryNeedsRefresh) {
  EXPECT_TRUE(tokenNeedsRefresh(NOW, 0, 0));
}

TEST(TokenExpiryTest, AlreadyExpiredNeedsRefresh) {
  EXPECT_TRUE(tokenNeedsRefresh(NOW, NOW - 1, NOW - 3600));
}

TEST(TokenExpiryTest, FreshFiveMinuteTokenIsUsable) {
  // Keycloak's default lifetime. The old fixed 10 minute margin treated
  // this as expired immediately and re-authenticated forever.
  EXPECT_FALSE(tokenNeedsRefresh(NOW, NOW + 300, NOW));
}

TEST(TokenExpiryTest, FiveMinuteTokenRefreshesInSecondHalf) {
  const long long iat = NOW - 151;
  EXPECT_TRUE(tokenNeedsRefresh(NOW, iat + 300, iat));
  const long long iat2 = NOW - 149;
  EXPECT_FALSE(tokenNeedsRefresh(NOW, iat2 + 300, iat2));
}

TEST(TokenExpiryTest, LongLivedTokenUsesTenMinuteMargin) {
  // One hour token: refresh once fewer than 10 minutes remain.
  EXPECT_FALSE(tokenNeedsRefresh(NOW, NOW + 601, NOW - 2999));
  EXPECT_TRUE(tokenNeedsRefresh(NOW, NOW + 599, NOW - 3001));
}

TEST(TokenExpiryTest, WithoutIssuedAtUsesOneMinuteMargin) {
  // Lifetime unknown: a fresh 5 minute token must still be usable.
  EXPECT_FALSE(tokenNeedsRefresh(NOW, NOW + 300, 0));
  EXPECT_FALSE(tokenNeedsRefresh(NOW, NOW + 61, 0));
  EXPECT_TRUE(tokenNeedsRefresh(NOW, NOW + 59, 0));
}
