#include "../textEncoding.hpp"

#include <sql.h>
#include <sqltypes.h>

#include <cstdint>
#include <stdexcept>
#include <string>

/*
POSIX (macOS / Linux) text conversions.

Trino returns UTF-8. The narrow (SQL_C_CHAR) encoding is platform-specific:
see src/platform/mac/ansiEncoding.cpp.

SQL_C_WCHAR data uses SQLWCHAR. With iODBC (the driver manager used by Excel
for Mac) SQLWCHAR is wchar_t, i.e. 4 bytes holding UTF-32. unixODBC uses a
2-byte SQLWCHAR (UTF-16) and would need a different conversion; fail the
build loudly instead of silently returning garbage.
*/
static_assert(sizeof(SQLWCHAR) == sizeof(wchar_t),
              "POSIX build expects SQLWCHAR == wchar_t (iODBC). "
              "unixODBC (2-byte SQLWCHAR) is not supported yet.");
static_assert(sizeof(wchar_t) == 4, "Expected a 4-byte wchar_t (UTF-32)");

namespace {

[[noreturn]] void throwInvalidUtf8() {
  throw std::runtime_error("Invalid UTF-8 returned by Trino");
}

} // namespace

std::wstring utf8ToWide(const std::string& value) {
  std::wstring result;
  result.reserve(value.size());

  const auto* bytes     = reinterpret_cast<const unsigned char*>(value.data());
  const std::size_t len = value.size();
  std::size_t i         = 0;

  while (i < len) {
    const unsigned char lead = bytes[i];
    uint32_t codePoint       = 0;
    std::size_t extra        = 0;
    uint32_t minValue        = 0;

    if (lead < 0x80) {
      codePoint = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      codePoint = lead & 0x1F;
      extra     = 1;
      minValue  = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      codePoint = lead & 0x0F;
      extra     = 2;
      minValue  = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      codePoint = lead & 0x07;
      extra     = 3;
      minValue  = 0x10000;
    } else {
      throwInvalidUtf8();
    }

    // The continuation bytes must all be present.
    if (i + extra >= len) {
      throwInvalidUtf8();
    }
    for (std::size_t k = 1; k <= extra; k++) {
      const unsigned char cont = bytes[i + k];
      if ((cont & 0xC0) != 0x80) {
        throwInvalidUtf8();
      }
      codePoint = (codePoint << 6) | (cont & 0x3F);
    }

    // Reject overlong encodings, UTF-16 surrogates and out-of-range values,
    // matching MultiByteToWideChar(MB_ERR_INVALID_CHARS) on Windows.
    if (codePoint < minValue || codePoint > 0x10FFFF ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
      throwInvalidUtf8();
    }

    result.push_back(static_cast<wchar_t>(codePoint));
    i += extra + 1;
  }
  return result;
}

std::string wideToUtf8(const std::wstring& value) {
  std::string result;
  result.reserve(value.size());
  for (wchar_t wc : value) {
    auto codePoint = static_cast<uint32_t>(wc);
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
      codePoint = 0xFFFD; // Replacement character.
    }
    if (codePoint < 0x80) {
      result.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
      result.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
      result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
      result.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
      result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
      result.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
  }
  return result;
}
