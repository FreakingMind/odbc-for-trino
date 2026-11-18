#include "gtest/gtest.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../src/util/b64decoder.hpp"
#include "../../../src/util/base64.hpp"

namespace {

std::vector<uint8_t> bytesOf(const std::string& s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

std::string stringOf(const std::vector<uint8_t>& v) {
  return std::string(v.begin(), v.end());
}

struct Rfc4648Vector {
    const char* plain;
    const char* encoded;
};

// RFC 4648, section 10.
const Rfc4648Vector RFC4648_VECTORS[] = {
    {"", ""},
    {"f", "Zg=="},
    {"fo", "Zm8="},
    {"foo", "Zm9v"},
    {"foob", "Zm9vYg=="},
    {"fooba", "Zm9vYmE="},
    {"foobar", "Zm9vYmFy"},
};

} // namespace

TEST(Base64Test, Rfc4648Encode) {
  for (const auto& v : RFC4648_VECTORS) {
    EXPECT_EQ(base64Encode(bytesOf(v.plain)), v.encoded)
        << "input: " << v.plain;
  }
}

TEST(Base64Test, Rfc4648Decode) {
  for (const auto& v : RFC4648_VECTORS) {
    EXPECT_EQ(stringOf(base64Decode(v.encoded)), v.plain)
        << "input: " << v.encoded;
  }
}

TEST(Base64Test, DecodeWithoutPadding) {
  EXPECT_EQ(stringOf(base64Decode("Zg")), "f");
  EXPECT_EQ(stringOf(base64Decode("Zm8")), "fo");
  EXPECT_EQ(stringOf(base64Decode("Zm9vYg")), "foob");
  EXPECT_EQ(stringOf(base64Decode("Zm9vYmE")), "fooba");
}

TEST(Base64Test, EncodeUsesStandardAlphabet) {
  // Bytes whose 6-bit groups are 62 / 63 map to '+' / '/'.
  std::vector<uint8_t> data = {0xFB, 0xEF, 0xBE, 0xFB, 0xEF, 0xBE};
  EXPECT_EQ(base64Encode(data), "++++++++");
  std::vector<uint8_t> data2 = {0xFF, 0xFF, 0xFF};
  EXPECT_EQ(base64Encode(data2), "////");
}

TEST(Base64Test, DecodeUrlSafeAlphabet) {
  std::vector<uint8_t> expected = {0xFB, 0xEF, 0xBE, 0xFF, 0xFF, 0xFF};
  EXPECT_EQ(base64Decode("----____"), expected);
  // Mixing both alphabets is accepted too.
  EXPECT_EQ(base64Decode("++++////"), expected);
  EXPECT_EQ(base64Decode("-+-+_/_/"), expected);
}

TEST(Base64Test, BinaryRoundTripAllByteValues) {
  std::vector<uint8_t> data;
  for (int i = 0; i < 256; i++) {
    data.push_back(static_cast<uint8_t>(i));
  }
  // Exercise every tail length (0, 1 and 2 leftover bytes).
  for (std::size_t len = data.size() - 3; len <= data.size(); len++) {
    std::vector<uint8_t> slice(data.begin(), data.begin() + len);
    std::string encoded = base64Encode(slice);
    EXPECT_EQ(encoded.size() % 4, 0u);
    EXPECT_EQ(base64Decode(encoded), slice) << "length " << len;
  }
}

TEST(Base64Test, EncodeHasNoLineBreaks) {
  std::vector<uint8_t> data(1000, 0x41);
  std::string encoded = base64Encode(data);
  EXPECT_EQ(encoded.find('\n'), std::string::npos);
  EXPECT_EQ(encoded.find('\r'), std::string::npos);
}

TEST(Base64Test, InvalidCharacterThrows) {
  EXPECT_THROW(base64Decode("Zm9v!"), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm 9v"), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm9v\n"), std::invalid_argument);
  EXPECT_THROW(base64Decode(std::string("Zm\0v", 4)), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm9v\xC3\xA9"), std::invalid_argument);
}

TEST(Base64Test, DataAfterPaddingThrows) {
  EXPECT_THROW(base64Decode("Zg==Zg=="), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm8=A"), std::invalid_argument);
  EXPECT_THROW(base64Decode("Z=g"), std::invalid_argument);
}

TEST(Base64Test, SingleTrailingDigitThrows) {
  EXPECT_THROW(base64Decode("Z"), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm9vY"), std::invalid_argument);
  EXPECT_THROW(base64Decode("Zm9vY==="), std::invalid_argument);
}

TEST(Base64urlDecoderTest, JwtPayloadWithUrlSafeChars) {
  // Unpadded base64url JWT payload segment of
  // {"sub":"~~~???>>>","name":"Тест"}; contains both '-' and '_'.
  const std::string segment =
      "eyJzdWIiOiJ-fn4_Pz8-Pj4iLCJuYW1lIjoi0KLQtdGB0YIifQ";
  const std::string payload =
      "{\"sub\":\"~~~???>>>\",\"name\":\"\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82\"}";
  ASSERT_NE(segment.find('-'), std::string::npos);
  ASSERT_NE(segment.find('_'), std::string::npos);
  EXPECT_EQ(fromBase64url(segment), payload);
}

TEST(Base64urlDecoderTest, KnownJwtSegment) {
  // Hand-checked: "<<???>>" -> PDw/Pz8+Pg (standard) -> PDw_Pz8-Pg (url).
  EXPECT_EQ(fromBase64url("PDw_Pz8-Pg"), "<<???>>");
}

TEST(Base64urlDecoderTest, InvalidInputThrowsRuntimeError) {
  EXPECT_THROW(fromBase64url("abc!"), std::runtime_error);
  EXPECT_THROW(fromBase64url("A"), std::runtime_error);
}
