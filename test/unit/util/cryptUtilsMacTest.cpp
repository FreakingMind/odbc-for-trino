#ifdef __APPLE__

#include "gtest/gtest.h"
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../src/util/base64.hpp"
#include "../../../src/util/cryptUtils.hpp"
#include "keychainTestFixture.hpp"

class CryptUtilsMacTest : public SecretStoreTest {};

TEST_F(CryptUtilsMacTest, EmptyStringRoundTrip) {
  EXPECT_EQ(userEncryptString(""), "");
  EXPECT_EQ(userDecryptString(""), "");
  EXPECT_EQ(systemEncryptString(""), "");
  EXPECT_EQ(systemDecryptString(""), "");
}

TEST_F(CryptUtilsMacTest, OutputIsPrintableBase64) {
  std::string encrypted = userEncryptString("secret");
  for (char c : encrypted) {
    bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    EXPECT_TRUE(valid) << "unexpected character '" << c << "'";
  }
}

TEST_F(CryptUtilsMacTest, EncryptionIsRandomized) {
  std::string secret = "sameSecret";
  EXPECT_NE(userEncryptString(secret), userEncryptString(secret));
}

TEST_F(CryptUtilsMacTest, LargeStringRoundTrip) {
  std::string secret;
  for (int i = 0; secret.size() < 10 * 1024; i++) {
    secret += "token-part-" + std::to_string(i) + ";";
  }
  EXPECT_EQ(userDecryptString(userEncryptString(secret)), secret);
  EXPECT_EQ(systemDecryptString(systemEncryptString(secret)), secret);
}

TEST_F(CryptUtilsMacTest, Utf8RoundTrip) {
  std::string secret = "Секретный пароль: ёжик 🦔 中文";
  EXPECT_EQ(userDecryptString(userEncryptString(secret)), secret);
  EXPECT_EQ(systemDecryptString(systemEncryptString(secret)), secret);
}

TEST_F(CryptUtilsMacTest, BinaryDataRoundTrip) {
  std::string secret("a\0b\xff\x01", 5);
  EXPECT_EQ(userDecryptString(userEncryptString(secret)), secret);
}

TEST_F(CryptUtilsMacTest, TamperedCiphertextThrows) {
  std::string encrypted     = userEncryptString("thisIsASecret");
  std::vector<uint8_t> blob = base64Decode(encrypted);
  ASSERT_GT(blob.size(), 40u);

  // Flip a bit in the ciphertext region (after magic, version and IV).
  std::vector<uint8_t> tampered = blob;
  tampered[21] ^= 0x01;
  EXPECT_THROW(userDecryptString(base64Encode(tampered)), std::runtime_error);

  // Flip a bit in the MAC.
  tampered = blob;
  tampered.back() ^= 0x80;
  EXPECT_THROW(userDecryptString(base64Encode(tampered)), std::runtime_error);

  // Truncate.
  tampered = blob;
  tampered.resize(blob.size() - 1);
  EXPECT_THROW(userDecryptString(base64Encode(tampered)), std::runtime_error);
}

TEST_F(CryptUtilsMacTest, WrongKeyLevelThrows) {
  std::string encrypted = userEncryptString("thisIsASecret");
  EXPECT_THROW(systemDecryptString(encrypted), std::runtime_error);
}

TEST_F(CryptUtilsMacTest, GarbageInputThrows) {
  EXPECT_THROW(userDecryptString("this is not base64!!"), std::runtime_error);
  EXPECT_THROW(userDecryptString("QUJDREVGR0g="), std::runtime_error);
  EXPECT_THROW(systemDecryptString("@@@"), std::runtime_error);
}

#endif // __APPLE__
