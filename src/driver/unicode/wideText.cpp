#include "wideText.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../../util/rowToBuffer.hpp"
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

bool isSurrogate(char32_t codePoint) {
  return codePoint >= 0xD800 && codePoint <= 0xDFFF;
}

bool isHighSurrogate(char32_t unit) {
  return unit >= 0xD800 && unit <= 0xDBFF;
}

bool isLowSurrogate(char32_t unit) {
  return unit >= 0xDC00 && unit <= 0xDFFF;
}

// Decodes one code point starting at bytes[i] and advances i. Invalid or
// truncated sequences, overlong forms, surrogates and values above U+10FFFF
// consume one byte and decode as U+FFFD.
char32_t decodeUtf8(std::string_view bytes, std::size_t& i) {
  const auto lead = static_cast<unsigned char>(bytes[i]);
  if (lead < 0x80) {
    i++;
    return lead;
  }

  std::size_t extra  = 0;
  char32_t codePoint = 0;
  char32_t minValue  = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra     = 1;
    codePoint = lead & 0x1F;
    minValue  = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    extra     = 2;
    codePoint = lead & 0x0F;
    minValue  = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    extra     = 3;
    codePoint = lead & 0x07;
    minValue  = 0x10000;
  } else {
    i++;
    return REPLACEMENT_CHARACTER;
  }

  // The continuation bytes must all be present.
  if (i + extra >= bytes.size()) {
    i++;
    return REPLACEMENT_CHARACTER;
  }
  for (std::size_t k = 1; k <= extra; k++) {
    const auto cont = static_cast<unsigned char>(bytes[i + k]);
    if ((cont & 0xC0) != 0x80) {
      i++;
      return REPLACEMENT_CHARACTER;
    }
    codePoint = (codePoint << 6) | (cont & 0x3F);
  }
  if (codePoint < minValue || codePoint > 0x10FFFF || isSurrogate(codePoint)) {
    i++;
    return REPLACEMENT_CHARACTER;
  }
  i += extra + 1;
  return codePoint;
}

void appendUtf8(std::string& out, char32_t codePoint) {
  if (codePoint > 0x10FFFF || isSurrogate(codePoint)) {
    codePoint = REPLACEMENT_CHARACTER;
  }
  if (codePoint < 0x80) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

} // namespace

template <typename Unit>
std::basic_string<Unit> utf8ToUtf(std::string_view utf8) {
  static_assert(std::is_same_v<Unit, char16_t> ||
                std::is_same_v<Unit, char32_t>);
  std::basic_string<Unit> result;
  result.reserve(utf8.size());
  std::size_t i = 0;
  while (i < utf8.size()) {
    const char32_t codePoint = decodeUtf8(utf8, i);
    if constexpr (std::is_same_v<Unit, char16_t>) {
      if (codePoint >= 0x10000) {
        const char32_t offset = codePoint - 0x10000;
        result.push_back(static_cast<char16_t>(0xD800 + (offset >> 10)));
        result.push_back(static_cast<char16_t>(0xDC00 + (offset & 0x3FF)));
        continue;
      }
    }
    result.push_back(static_cast<Unit>(codePoint));
  }
  return result;
}

template <typename Unit>
std::string utfToUtf8(std::basic_string_view<Unit> text) {
  static_assert(std::is_same_v<Unit, char16_t> ||
                std::is_same_v<Unit, char32_t>);
  std::string result;
  result.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); i++) {
    char32_t unit = text[i];
    if constexpr (std::is_same_v<Unit, char16_t>) {
      if (isHighSurrogate(unit) && i + 1 < text.size() &&
          isLowSurrogate(text[i + 1])) {
        const char32_t low = text[i + 1];
        unit               = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        i++;
      }
    }
    appendUtf8(result, unit);
  }
  return result;
}

template <typename Unit>
std::size_t utfCharBoundary(std::basic_string_view<Unit> text,
                            std::size_t maxUnits) {
  if (maxUnits >= text.size()) {
    return text.size();
  }
  if constexpr (std::is_same_v<Unit, char16_t>) {
    // Don't keep a high surrogate whose low half would be cut off.
    if (maxUnits > 0 && isHighSurrogate(text[maxUnits - 1]) &&
        isLowSurrogate(text[maxUnits])) {
      return maxUnits - 1;
    }
  }
  return maxUnits;
}

template std::u16string utf8ToUtf<char16_t>(std::string_view);
template std::u32string utf8ToUtf<char32_t>(std::string_view);
template std::string utfToUtf8<char16_t>(std::u16string_view);
template std::string utfToUtf8<char32_t>(std::u32string_view);
template std::size_t utfCharBoundary<char16_t>(std::u16string_view,
                                               std::size_t);
template std::size_t utfCharBoundary<char32_t>(std::u32string_view,
                                               std::size_t);

WideTextCodec wideTextCodecFor(AnsiEncoding ansiEncoding) {
  WideTextCodec codec;
  if (sizeof(SQLWCHAR) == 4 && !(ansiEncoding == utf8AnsiEncoding())) {
    codec.widenedAnsi = ansiEncoding;
  }
  return codec;
}

template <typename Unit>
std::optional<std::string> widenedAnsiToUtf8(std::basic_string_view<Unit> text,
                                             AnsiEncoding encoding) {
  std::string bytes;
  bytes.reserve(text.size());
  bool hasHighByte = false;
  for (const Unit unit : text) {
    const auto value = static_cast<std::uint32_t>(unit);
    if (value > 0xFF) {
      return std::nullopt;
    }
    hasHighByte = hasHighByte || value >= 0x80;
    bytes.push_back(static_cast<char>(static_cast<unsigned char>(value)));
  }
  if (!hasHighByte) {
    return std::nullopt;
  }
  return ansiToUtf8(bytes, encoding);
}

template <typename Unit>
std::basic_string<Unit> utf8ToWidenedAnsi(std::string_view utf8,
                                          AnsiEncoding encoding) {
  // Round trip through UTF-32 first: invalid UTF-8 becomes U+FFFD (then
  // '?') instead of an exception, as on the Unicode path.
  const std::string bytes =
      utf8ToAnsi(utfToUtf8<char32_t>(utf8ToUtf<char32_t>(utf8)), encoding);
  std::basic_string<Unit> result;
  result.reserve(bytes.size());
  for (const char byte : bytes) {
    result.push_back(static_cast<Unit>(static_cast<unsigned char>(byte)));
  }
  return result;
}

template std::optional<std::string>
    widenedAnsiToUtf8<char16_t>(std::u16string_view, AnsiEncoding);
template std::optional<std::string>
    widenedAnsiToUtf8<char32_t>(std::u32string_view, AnsiEncoding);
template std::u16string utf8ToWidenedAnsi<char16_t>(std::string_view,
                                                    AnsiEncoding);
template std::u32string utf8ToWidenedAnsi<char32_t>(std::string_view,
                                                    AnsiEncoding);

std::size_t sqlWideLength(const SQLWCHAR* text) {
  std::size_t length = 0;
  if (text) {
    while (text[length] != 0) {
      length++;
    }
  }
  return length;
}

std::string sqlWideToUtf8(const SQLWCHAR* text,
                          SQLLEN lengthChars,
                          const WideTextCodec& codec) {
  if (!text) {
    return std::string();
  }
  std::size_t length = 0;
  if (lengthChars == SQL_NTS) {
    length = sqlWideLength(text);
  } else if (lengthChars > 0) {
    length = static_cast<std::size_t>(lengthChars);
  }
  // SQLWCHAR may be a signed type (wchar_t); copy the bits into the
  // unsigned code unit type of the same size.
  SqlWideString units(length, SqlWideUnit{0});
  if (length > 0) {
    std::memcpy(units.data(), text, length * sizeof(SQLWCHAR));
  }
  // Widened bytes that aren't valid in the encoding are read as Unicode
  // (Latin-1) too, like the input of a W application.
  if (codec.widenedAnsi) {
    if (std::optional<std::string> narrow =
            widenedAnsiToUtf8<SqlWideUnit>(units, *codec.widenedAnsi)) {
      return *std::move(narrow);
    }
  }
  return utfToUtf8<SqlWideUnit>(units);
}

SqlWideString utf8ToSqlWide(std::string_view utf8, const WideTextCodec& codec) {
  if (codec.widenedAnsi) {
    return utf8ToWidenedAnsi<SqlWideUnit>(utf8, *codec.widenedAnsi);
  }
  return utf8ToUtf<SqlWideUnit>(utf8);
}

WideCopyResult copyToSqlWideBuffer(const SqlWideString& text,
                                   SQLWCHAR* buffer,
                                   std::size_t capacityUnits,
                                   const WideTextCodec& codec) {
  WideCopyResult result;
  result.totalUnits = text.size();
  if (!buffer) {
    return result;
  }
  if (capacityUnits == 0) {
    // No room even for the terminator.
    result.truncated = true;
    return result;
  }
  std::size_t copied = 0;
  const bool narrowText =
      codec.widenedAnsi && std::all_of(text.begin(), text.end(), [](auto unit) {
        return static_cast<std::uint32_t>(unit) <= 0xFF;
      });
  if (narrowText) {
    // One unit per byte: cut between characters of the narrow encoding.
    std::string bytes;
    bytes.reserve(text.size());
    for (const SqlWideUnit unit : text) {
      bytes.push_back(static_cast<char>(static_cast<unsigned char>(unit)));
    }
    const SQLLEN safe = calculateAnsiSafeCopyLength(
        bytes, 0, static_cast<SQLLEN>(capacityUnits - 1), *codec.widenedAnsi);
    copied = safe > 0 ? static_cast<std::size_t>(safe) : 0;
  } else {
    copied = utfCharBoundary<SqlWideUnit>(text, capacityUnits - 1);
  }
  if (copied > 0) {
    std::memcpy(buffer, text.data(), copied * sizeof(SQLWCHAR));
  }
  buffer[copied]   = 0;
  result.truncated = copied < text.size();
  return result;
}
