#pragma once

#include <cstdint>
#include <string>
#include <vector>

/*
Portable base64 helpers (RFC 4648). These replace the Windows-only
CryptBinaryToString / CryptStringToBinary calls so the same code works
on every platform.
*/

// Standard base64 with '=' padding and no line breaks.
std::string base64Encode(const std::vector<uint8_t>& data);

// Decodes standard ('+', '/') or URL-safe ('-', '_') base64. Padding is
// optional. Throws std::invalid_argument on malformed input.
std::vector<uint8_t> base64Decode(const std::string& encoded);
