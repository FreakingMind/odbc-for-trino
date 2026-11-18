#include <gtest/gtest.h>

#include "../../../src/util/redactSecrets.hpp"

TEST(RedactSecretsTest, RedactsCredentialLikeKeys) {
  std::map<std::string, std::string> in = {{"clientsecret", "s3cr3t"},
                                           {"clientSecret", "s3cr3t"},
                                           {"PWD", "pw"},
                                           {"password", "pw"},
                                           {"accessToken", "eyJ..."},
                                           {"hostname", "https://example"},
                                           {"clientid", "my-client"}};
  auto out                              = redactSecretsForLog(in);
  EXPECT_EQ(out["clientsecret"], "***");
  EXPECT_EQ(out["clientSecret"], "***");
  EXPECT_EQ(out["PWD"], "***");
  EXPECT_EQ(out["password"], "***");
  EXPECT_EQ(out["accessToken"], "***");
  EXPECT_EQ(out["hostname"], "https://example");
  EXPECT_EQ(out["clientid"], "my-client");
}

TEST(RedactSecretsTest, KeepsEmptyValuesEmpty) {
  auto out = redactSecretsForLog({{"clientsecret", ""}});
  EXPECT_EQ(out["clientsecret"], "");
}
