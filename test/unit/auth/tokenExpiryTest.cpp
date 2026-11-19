#include <gtest/gtest.h>

#include "../../../src/trinoAPIWrapper/authProvider/tokens/tokenCache.hpp"
#include "../../../src/trinoAPIWrapper/authProvider/tokens/tokenParser.hpp"

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

TEST(TokenExpiryTest, NoTokenNeedsOne) {
  EXPECT_TRUE(accessTokenNeedsRefresh(NOW, "", json::object(), NOW, 0));
}

TEST(TokenExpiryTest, JwtUsesItsClaims) {
  const json claims = {{"exp", NOW + 3600}, {"iat", NOW}};
  // The configured lifetime only applies to tokens without "exp".
  EXPECT_FALSE(accessTokenNeedsRefresh(NOW, "a.b.c", claims, NOW - 7200, 60));
  const json expired = {{"exp", NOW - 1}, {"iat", NOW - 3600}};
  EXPECT_TRUE(accessTokenNeedsRefresh(NOW, "a.b.c", expired, NOW, 0));
}

TEST(TokenExpiryTest, OpaqueTokenWithoutLifetimeIsKeptUntilRejected) {
  EXPECT_FALSE(accessTokenNeedsRefresh(
      NOW, "opaque", json::object(), NOW - 1'000'000, 0));
  // Even when its age is unknown.
  EXPECT_FALSE(accessTokenNeedsRefresh(NOW, "opaque", json::object(), 0, 0));
}

TEST(TokenExpiryTest, OpaqueTokenUsesConfiguredLifetime) {
  const long long eightHours = 8 * 3600;
  EXPECT_FALSE(accessTokenNeedsRefresh(
      NOW, "opaque", json::object(), NOW - 7 * 3600, eightHours));
  // Renewed 10 minutes ahead, like a JWT.
  EXPECT_TRUE(accessTokenNeedsRefresh(
      NOW, "opaque", json::object(), NOW - eightHours + 599, eightHours));
  // Age unknown (cached by an older driver): renew.
  EXPECT_TRUE(
      accessTokenNeedsRefresh(NOW, "opaque", json::object(), 0, eightHours));
}

TEST(TokenParserTest, ReadsJwtClaims) {
  // {"alg":"none"} . {"exp":1800003600,"sub":"alice"} . (no signature)
  const json claims = parseAccessToken(
      "eyJhbGciOiJub25lIn0.eyJleHAiOjE4MDAwMDM2MDAsInN1YiI6ImFsaWNlIn0.");
  EXPECT_EQ(claims.value("exp", 0LL), 1800003600LL);
  EXPECT_EQ(claims.value("sub", std::string()), "alice");
}

TEST(TokenParserTest, OpaqueAndEncryptedTokensHaveNoClaims) {
  // A JWE (five parts, encrypted payload), an opaque string, and a JWS
  // whose payload isn't JSON: no exceptions, no claims.
  for (const char* token : {"eyJhbGciOiJkaXIifQ..aXY.Y2lwaGVy.dGFn",
                            "opaque-token",
                            "a.bm90IGpzb24.c",
                            ""}) {
    const json claims = parseAccessToken(token);
    EXPECT_TRUE(claims.is_object()) << token;
    EXPECT_TRUE(claims.empty()) << token;
  }
}
