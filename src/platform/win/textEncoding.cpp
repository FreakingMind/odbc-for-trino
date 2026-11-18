#include "../textEncoding.hpp"

#include "../../util/rowToBuffer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../util/stringTrim.hpp"

/*
Windows text conversions: Trino returns UTF-8; SQL_C_CHAR consumers expect
the system ANSI code page and SQL_C_WCHAR consumers expect UTF-16.
*/

std::wstring utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return std::wstring();
  }

  const int required = MultiByteToWideChar(CP_UTF8,
                                           MB_ERR_INVALID_CHARS,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0);
  if (required <= 0) {
    throw std::runtime_error("Invalid UTF-8 returned by Trino");
  }

  std::wstring result(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(CP_UTF8,
                                            MB_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            result.data(),
                                            required);
  if (converted != required) {
    throw std::runtime_error("Failed to convert Trino UTF-8 to UTF-16");
  }
  return result;
}


namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return value;
}

} // namespace

AnsiEncoding utf8AnsiEncoding() {
  return AnsiEncoding{CP_UTF8};
}

AnsiEncoding resolveAnsiEncoding(const std::string& name) {
  std::string trimmed = name;
  trim(trimmed);
  const std::string lower = toLower(trimmed);
  if (lower.empty() || lower == "auto") {
    // The ANSI code page Excel and other ANSI applications use. Every
    // Windows ANSI code page is ASCII-compatible.
    return AnsiEncoding{GetACP()};
  }
  if (lower == "utf-8" || lower == "utf8") {
    return utf8AnsiEncoding();
  }
  throw std::invalid_argument("Invalid ansiEncoding value '" + trimmed +
                              "': use auto or UTF-8");
}

std::string ansiEncodingName(AnsiEncoding encoding) {
  return encoding.id == CP_UTF8 ? "UTF-8"
                                : "code page " + std::to_string(encoding.id);
}

std::string
utf8ToAnsi(const std::string& value, AnsiEncoding encoding, bool* replaced) {
  if (replaced != nullptr) {
    *replaced = false;
  }
  // When the ANSI encoding is UTF-8, the bytes returned by Trino are already
  // in the exact encoding expected by SQL_C_CHAR consumers.
  if (encoding.id == CP_UTF8) {
    return value;
  }

  const std::wstring wide = utf8ToWide(value);
  if (wide.empty()) {
    return std::string();
  }

  const UINT codePage  = static_cast<UINT>(encoding.id);
  BOOL usedDefaultChar = FALSE;
  const int required   = WideCharToMultiByte(codePage,
                                             WC_NO_BEST_FIT_CHARS,
                                             wide.data(),
                                             static_cast<int>(wide.size()),
                                             nullptr,
                                             0,
                                             "?",
                                             &usedDefaultChar);
  if (required <= 0) {
    throw std::runtime_error("Failed to calculate ANSI output length");
  }

  std::string result(static_cast<std::size_t>(required), '\0');
  const int converted = WideCharToMultiByte(codePage,
                                            WC_NO_BEST_FIT_CHARS,
                                            wide.data(),
                                            static_cast<int>(wide.size()),
                                            result.data(),
                                            required,
                                            "?",
                                            &usedDefaultChar);
  if (converted != required) {
    throw std::runtime_error("Failed to convert Trino UTF-8 to Windows ANSI");
  }

  if (replaced != nullptr) {
    *replaced = usedDefaultChar != FALSE;
  }
  return result;
}


std::optional<std::string> ansiToUtf8(std::string_view bytes,
                                      AnsiEncoding encoding) {
  if (bytes.empty()) {
    return std::string();
  }
  const UINT codePage = static_cast<UINT>(encoding.id);
  const int required  = MultiByteToWideChar(codePage,
                                            MB_ERR_INVALID_CHARS,
                                            bytes.data(),
                                            static_cast<int>(bytes.size()),
                                            nullptr,
                                            0);
  if (required <= 0) {
    return std::nullopt;
  }
  std::wstring wide(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(codePage,
                          MB_ERR_INVALID_CHARS,
                          bytes.data(),
                          static_cast<int>(bytes.size()),
                          wide.data(),
                          required) != required) {
    return std::nullopt;
  }
  return wideToUtf8(wide);
}

SQLLEN calculateAnsiSafeCopyLength(const std::string& value,
                                   std::size_t byteOffset,
                                   SQLLEN maxBytes,
                                   AnsiEncoding encoding) {
  if (maxBytes <= 0 || byteOffset >= value.size()) {
    return 0;
  }

  if (encoding.id == CP_UTF8) {
    return calculateUtf8SafeCopyLength(value, byteOffset, maxBytes);
  }

  const UINT codePage     = static_cast<UINT>(encoding.id);
  const std::size_t limit = std::min<std::size_t>(
      value.size(), byteOffset + static_cast<std::size_t>(maxBytes));
  std::size_t pos = byteOffset;
  while (pos < limit) {
    std::size_t charBytes = 1;
    if (IsDBCSLeadByteEx(codePage, static_cast<BYTE>(value[pos])) &&
        pos + 1 < value.size()) {
      charBytes = 2;
    }
    if (pos + charBytes > limit) {
      break;
    }
    pos += charBytes;
  }
  return static_cast<SQLLEN>(pos - byteOffset);
}


std::string wideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return std::string();
  }
  // Without WC_ERR_INVALID_CHARS, unpaired surrogates become U+FFFD.
  const int required = WideCharToMultiByte(CP_UTF8,
                                           0,
                                           value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr);
  if (required <= 0) {
    return std::string();
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8,
                      0,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      required,
                      nullptr,
                      nullptr);
  return result;
}
