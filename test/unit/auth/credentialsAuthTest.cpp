#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "../../../src/trinoAPIWrapper/authProvider/credentialsAuthProvider.hpp"
#include "../../../src/trinoAPIWrapper/trinoExceptions.hpp"

namespace {

std::string headerOf(AuthConfig& config, const std::string& name) {
  auto it = config.headers.find(name);
  return it == config.headers.end() ? "" : it->second;
}

const std::string HTTPS = "https://trino.example.com";

} // namespace

TEST(CredentialsAuthTest, BasicAuthorizationValue) {
  // RFC 7617 example.
  EXPECT_EQ(basicAuthorizationValue("Aladdin", "open sesame"),
            "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
  // The password may contain ':' (only the first one separates).
  EXPECT_EQ(basicAuthorizationValue("alice", "a:b"), "Basic YWxpY2U6YTpi");
}

TEST(CredentialsAuthTest, PasswordProviderSetsHeaders) {
  auto config = getPasswordAuthConfigPtr(
      HTTPS, 443, "dsn", AuthCredentials{"alice", "secret", ""});
  EXPECT_EQ(headerOf(*config, "Authorization"),
            basicAuthorizationValue("alice", "secret"));
  EXPECT_EQ(headerOf(*config, "X-Trino-User"), "alice");
  EXPECT_FALSE(config->isExpired());
}

TEST(CredentialsAuthTest, AccessTokenProviderSetsHeaders) {
  auto config = getAccessTokenAuthConfigPtr(
      HTTPS, 443, "dsn", AuthCredentials{"", "", "eyJ.token"});
  EXPECT_EQ(headerOf(*config, "Authorization"), "Bearer eyJ.token");
  // Trino takes the user from the token unless one is given.
  EXPECT_EQ(config->headers.count("X-Trino-User"), 0u);
  EXPECT_FALSE(config->isExpired());
}

TEST(CredentialsAuthTest, AccessTokenFromPasswordWins) {
  // Excel only asks for a user name and password: a token typed there wins
  // over one stored in the data source (which may have expired).
  EXPECT_EQ(effectiveAccessToken(AuthCredentials{"", "from-pwd", ""}),
            "from-pwd");
  EXPECT_EQ(effectiveAccessToken(AuthCredentials{"", "from-pwd", "stored"}),
            "from-pwd");
  EXPECT_EQ(effectiveAccessToken(AuthCredentials{"", "", "stored"}), "stored");
  // Pasted tokens are trimmed.
  EXPECT_EQ(effectiveAccessToken(AuthCredentials{"", " tok\n", ""}), "tok");
  EXPECT_EQ(effectiveAccessToken(AuthCredentials{"", " \t ", ""}), "");
  auto config = getAccessTokenAuthConfigPtr(
      HTTPS, 443, "dsn", AuthCredentials{"bob", "from-pwd", ""});
  EXPECT_EQ(headerOf(*config, "Authorization"), "Bearer from-pwd");
  EXPECT_EQ(headerOf(*config, "X-Trino-User"), "bob");
}

TEST(CredentialsAuthTest, PasswordNeedsUserAndPassword) {
  EXPECT_THROW(checkCredentials(AM_PASSWORD, HTTPS, {"", "secret", ""}),
               AuthError);
  EXPECT_THROW(checkCredentials(AM_PASSWORD, HTTPS, {"alice", "", ""}),
               AuthError);
  EXPECT_THROW(checkCredentials(AM_PASSWORD, HTTPS, {"a:b", "secret", ""}),
               AuthError);
  EXPECT_NO_THROW(checkCredentials(AM_PASSWORD, HTTPS, {"alice", "x", ""}));
}

TEST(CredentialsAuthTest, AccessTokenNeedsToken) {
  EXPECT_THROW(checkCredentials(AM_ACCESS_TOKEN, HTTPS, {"", "", ""}),
               AuthError);
  EXPECT_THROW(checkCredentials(AM_ACCESS_TOKEN, HTTPS, {"", "", "a\r\nX: y"}),
               AuthError);
  EXPECT_NO_THROW(checkCredentials(AM_ACCESS_TOKEN, HTTPS, {"", "", "t"}));
}

TEST(CredentialsAuthTest, CredentialsOnlyOverHttps) {
  for (const char* host :
       {"http://trino.example.com", "trino.example.com", "localhost"}) {
    EXPECT_THROW(checkCredentials(AM_PASSWORD, host, {"alice", "x", ""}),
                 AuthError)
        << host;
    EXPECT_THROW(checkCredentials(AM_ACCESS_TOKEN, host, {"", "", "t"}),
                 AuthError)
        << host;
  }
  EXPECT_NO_THROW(checkCredentials(
      AM_PASSWORD, "HTTPS://Trino.Example.com", {"a", "x", ""}));
  // No Auth sends no secret: plain HTTP is fine.
  EXPECT_NO_THROW(
      checkCredentials(AM_NO_AUTH, "http://localhost", {"alice", "", ""}));
}

TEST(CredentialsAuthTest, UserWithLineBreakRejected) {
  EXPECT_THROW(checkCredentials(
                   AM_NO_AUTH, "http://localhost", {"a\nX-Evil: 1", "", ""}),
               AuthError);
}

TEST(SqlStateTest, MapsExceptionTypes) {
  EXPECT_STREQ(sqlStateFor(AuthError("no")), "28000");
  EXPECT_STREQ(sqlStateFor(ConnectionError("down")), "08S01");
  EXPECT_STREQ(sqlStateFor(std::runtime_error("other")), "HY000");
  EXPECT_STREQ(sqlStateFor(std::invalid_argument("bad")), "HY000");
}
