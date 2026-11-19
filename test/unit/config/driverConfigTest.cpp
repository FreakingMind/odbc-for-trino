#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>

#include "../../../src/driver/config/driverConfig.hpp"

TEST(DriverConfigTest, ParsesNewAuthMethods) {
  DriverConfig config;
  config.setAuthMethod("password");
  EXPECT_EQ(config.getAuthMethodEnum(), AM_PASSWORD);
  EXPECT_EQ(config.getAuthMethodStr(), "Password");
  config.setAuthMethod("ACCESS TOKEN");
  EXPECT_EQ(config.getAuthMethodEnum(), AM_ACCESS_TOKEN);
  EXPECT_EQ(config.getAuthMethodStr(), "Access Token");
}

TEST(DriverConfigTest, UnknownAuthMethodNamesTheChoices) {
  DriverConfig config;
  try {
    config.setAuthMethod("Kerberos");
    FAIL() << "expected an exception";
  } catch (const std::invalid_argument& e) {
    const std::string message = e.what();
    EXPECT_NE(message.find("Kerberos"), std::string::npos) << message;
    EXPECT_NE(message.find("Password"), std::string::npos) << message;
  }
}

TEST(DriverConfigTest, ReadsCredentialKeywords) {
  DriverConfig config = driverConfigFromKVPs({{"user", " alice "},
                                              {"password", " pass word "},
                                              {"accesstoken", "tok"}});
  EXPECT_EQ(config.getUser(), "alice");
  // Spaces can be part of a password.
  EXPECT_EQ(config.getPassword(), " pass word ");
  EXPECT_EQ(config.getAccessToken(), "tok");
}

TEST(DriverConfigTest, UidAndPwdOverrideTheDataSource) {
  // As SQLDriverConnect merges them: the data source's values (user,
  // password) plus the application's UID/PWD.
  DriverConfig config = driverConfigFromKVPs({{"user", "dsn-user"},
                                              {"password", "dsn-pw"},
                                              {"uid", "app"},
                                              {"pwd", "app-pw"}});
  EXPECT_EQ(config.getUser(), "app");
  EXPECT_EQ(config.getPassword(), "app-pw");
}

TEST(DriverConfigTest, EmptyUidAndPwdKeepTheDataSource) {
  // Excel passes "UID=;PWD=" when its login prompt is left empty.
  DriverConfig config = driverConfigFromKVPs(
      {{"user", "dsn-user"}, {"password", "dsn-pw"}, {"uid", ""}, {"pwd", ""}});
  EXPECT_EQ(config.getUser(), "dsn-user");
  EXPECT_EQ(config.getPassword(), "dsn-pw");
}

TEST(DriverConfigTest, CredentialsRoundTripThroughKvps) {
  DriverConfig config;
  config.setUser("alice");
  config.setPassword("pw");
  config.setAccessToken("tok");
  std::map<std::string, std::string> kvps = driverConfigToKVPs(config);
  EXPECT_EQ(kvps.at("user"), "alice");
  EXPECT_EQ(kvps.at("password"), "pw");
  EXPECT_EQ(kvps.at("accessToken"), "tok");
  DriverConfig copy = driverConfigFromKVPs(kvps);
  EXPECT_EQ(copy.getUser(), "alice");
  EXPECT_EQ(copy.getPassword(), "pw");
  EXPECT_EQ(copy.getAccessToken(), "tok");
}

TEST(DriverConfigTest, EmptyCredentialsAreNotWritten) {
  std::map<std::string, std::string> kvps = driverConfigToKVPs(DriverConfig());
  EXPECT_EQ(kvps.count("user"), 0u);
  EXPECT_EQ(kvps.count("password"), 0u);
  EXPECT_EQ(kvps.count("accessToken"), 0u);
}

TEST(DriverConfigTest, ConnectionStringTokenWinsOverDsnToken) {
  // The merged map of SQLDriverConnect: the DSN's camelCase key plus the
  // connection string's lowercase one.
  DriverConfig config = driverConfigFromKVPs(
      {{"accessToken", "from-dsn"}, {"accesstoken", "from-connection-string"}});
  EXPECT_EQ(config.getAccessToken(), "from-connection-string");
}

TEST(DriverConfigTest, TokenLifetime) {
  EXPECT_EQ(driverConfigFromKVPs({}).getTokenLifetime(), 0);
  EXPECT_EQ(
      driverConfigFromKVPs({{"tokenlifetime", " 28800 "}}).getTokenLifetime(),
      28800);
  EXPECT_EQ(driverConfigFromKVPs({{"tokenLifetime", ""}}).getTokenLifetime(),
            0);
  EXPECT_THROW(driverConfigFromKVPs({{"tokenlifetime", "8h"}}),
               std::invalid_argument);
  EXPECT_THROW(driverConfigFromKVPs({{"tokenlifetime", "-1"}}),
               std::invalid_argument);
  DriverConfig config = driverConfigFromKVPs({{"tokenlifetime", "3600"}});
  EXPECT_EQ(driverConfigToKVPs(config).at("tokenLifetime"), "3600");
}
