#include "base64.hpp"

#include <array>
#include <stdexcept>

namespace {

constexpr char ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr int INVALID = -1;
constexpr int PADDING = -2;

constexpr std::array<int, 256> makeDecodeTable() {
  std::array<int, 256> table{};
  for (auto& entry : table) {
    entry = INVALID;
  }
  for (int i = 0; i < 64; i++) {
    table[static_cast<unsigned char>(ALPHABET[i])] = i;
  }
  // Accept the URL-safe alphabet as well.
  table[static_cast<unsigned char>('-')] = 62;
  table[static_cast<unsigned char>('_')] = 63;
  table[static_cast<unsigned char>('=')] = PADDING;
  return table;
}

constexpr std::array<int, 256> DECODE_TABLE = makeDecodeTable();

} // namespace

std::string base64Encode(const std::vector<uint8_t>& data) {
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);

  std::size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                      (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
    out.push_back(ALPHABET[(triple >> 18) & 0x3F]);
    out.push_back(ALPHABET[(triple >> 12) & 0x3F]);
    out.push_back(ALPHABET[(triple >> 6) & 0x3F]);
    out.push_back(ALPHABET[triple & 0x3F]);
  }

  std::size_t remaining = data.size() - i;
  if (remaining == 1) {
    uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(ALPHABET[(triple >> 18) & 0x3F]);
    out.push_back(ALPHABET[(triple >> 12) & 0x3F]);
    out.append("==");
  } else if (remaining == 2) {
    uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                      (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(ALPHABET[(triple >> 18) & 0x3F]);
    out.push_back(ALPHABET[(triple >> 12) & 0x3F]);
    out.push_back(ALPHABET[(triple >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
  std::vector<uint8_t> out;
  out.reserve((encoded.size() / 4) * 3 + 3);

  uint32_t buffer    = 0;
  int bitsCollected  = 0;
  bool paddingSeen   = false;
  std::size_t digits = 0;

  for (char c : encoded) {
    int value = DECODE_TABLE[static_cast<unsigned char>(c)];
    if (value == INVALID) {
      throw std::invalid_argument("Invalid character in base64 input");
    }
    if (value == PADDING) {
      paddingSeen = true;
      continue;
    }
    if (paddingSeen) {
      throw std::invalid_argument("Data after padding in base64 input");
    }
    digits++;
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bitsCollected += 6;
    if (bitsCollected >= 8) {
      bitsCollected -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
    }
  }

  // A single trailing base64 digit can't encode a whole byte.
  if (digits % 4 == 1) {
    throw std::invalid_argument("Truncated base64 input");
  }
  return out;
}
