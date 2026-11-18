#include "gtest/gtest.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../src/platform/textEncoding.hpp"
#include "../../../src/util/rowToBuffer.hpp"
#ifdef __APPLE__
#include "../../../src/platform/mac/ansiEncoding.hpp"
#endif

/*
Tests for the UTF-8 -> SQL_C_CHAR / SQL_C_WCHAR conversions and the chunk
length helpers used by SQLGetData / SQLFetch. Expectations are written to
hold on both POSIX (4-byte SQLWCHAR, UTF-32) and Windows (2-byte SQLWCHAR,
UTF-16).
*/

namespace {

// "Привет, мир"
const std::string CYRILLIC_UTF8 =
    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, "
    "\xD0\xBC\xD0\xB8\xD1\x80";
const std::wstring CYRILLIC_WIDE = L"Привет, мир";

// "中文" (3-byte UTF-8 sequences)
const std::string CJK_UTF8  = "\xE4\xB8\xAD\xE6\x96\x87";
const std::wstring CJK_WIDE = L"中文";

// U+1F600 (4-byte UTF-8 sequence, surrogate pair in UTF-16)
const std::string EMOJI_UTF8  = "\xF0\x9F\x98\x80";
const std::wstring EMOJI_WIDE = L"\U0001F600";

constexpr bool WIDE_IS_UTF16 = sizeof(SQLWCHAR) == 2;

constexpr SQLLEN unitBytes(std::size_t units) {
  return static_cast<SQLLEN>(units * sizeof(SQLWCHAR));
}

} // namespace

// ---------------------------------------------------------------------------
// utf8ToWide
// ---------------------------------------------------------------------------

TEST(TextEncodingTest, WideEmpty) {
  EXPECT_EQ(utf8ToWide(""), L"");
}

TEST(TextEncodingTest, WideAscii) {
  EXPECT_EQ(utf8ToWide("Hello, world!"), L"Hello, world!");
}

TEST(TextEncodingTest, WideCyrillic) {
  std::wstring wide = utf8ToWide(CYRILLIC_UTF8);
  EXPECT_EQ(wide, CYRILLIC_WIDE);
  EXPECT_EQ(wide.size(), 11u);
}

TEST(TextEncodingTest, WideThreeByte) {
  EXPECT_EQ(utf8ToWide(CJK_UTF8), CJK_WIDE);
  EXPECT_EQ(utf8ToWide(CJK_UTF8).size(), 2u);
}

TEST(TextEncodingTest, WideFourByte) {
  std::wstring wide = utf8ToWide(EMOJI_UTF8);
  EXPECT_EQ(wide, EMOJI_WIDE);
  if constexpr (WIDE_IS_UTF16) {
    ASSERT_EQ(wide.size(), 2u);
    EXPECT_EQ(static_cast<unsigned>(wide[0]), 0xD83Du);
    EXPECT_EQ(static_cast<unsigned>(wide[1]), 0xDE00u);
  } else {
    ASSERT_EQ(wide.size(), 1u);
    EXPECT_EQ(static_cast<unsigned long>(wide[0]), 0x1F600ul);
  }
}

TEST(TextEncodingTest, WideBoundaryCodePoints) {
  // U+007F, U+0080, U+07FF, U+0800, U+FFFF, U+10000, U+10FFFF
  EXPECT_EQ(utf8ToWide("\x7F"), L"\u007F");
  EXPECT_EQ(utf8ToWide("\xC2\x80"), L"\u0080");
  EXPECT_EQ(utf8ToWide("\xDF\xBF"), L"߿");
  EXPECT_EQ(utf8ToWide("\xE0\xA0\x80"), L"ࠀ");
  EXPECT_EQ(utf8ToWide("\xEF\xBF\xBF"), L"￿");
  EXPECT_EQ(utf8ToWide("\xF0\x90\x80\x80"), L"\U00010000");
  EXPECT_EQ(utf8ToWide("\xF4\x8F\xBF\xBF"), L"\U0010FFFF");
}

TEST(TextEncodingTest, WideMixed) {
  std::string utf8      = "a" + CYRILLIC_UTF8 + CJK_UTF8 + EMOJI_UTF8 + "z";
  std::wstring expected = L"a" + CYRILLIC_WIDE + CJK_WIDE + EMOJI_WIDE + L"z";
  EXPECT_EQ(utf8ToWide(utf8), expected);
}

TEST(TextEncodingTest, WideEmbeddedNul) {
  std::string utf8("a\0b", 3);
  std::wstring expected(L"a\0b", 3);
  EXPECT_EQ(utf8ToWide(utf8), expected);
}

TEST(TextEncodingTest, WideTruncatedSequenceThrows) {
  EXPECT_THROW(utf8ToWide("\xD0"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("abc\xE4\xB8"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xF0\x9F\x98"), std::runtime_error);
  // Truncated in the middle, followed by ASCII.
  EXPECT_THROW(utf8ToWide("\xE4\xB8z"), std::runtime_error);
}

TEST(TextEncodingTest, WideStrayContinuationThrows) {
  EXPECT_THROW(utf8ToWide("\x80"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("ab\xBF"
                          "cd"),
               std::runtime_error);
}

TEST(TextEncodingTest, WideOverlongThrows) {
  EXPECT_THROW(utf8ToWide("\xC0\xAF"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xC1\xBF"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xE0\x80\xAF"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xF0\x80\x80\xAF"), std::runtime_error);
}

TEST(TextEncodingTest, WideEncodedSurrogateThrows) {
  EXPECT_THROW(utf8ToWide("\xED\xA0\x80"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xED\xBF\xBF"), std::runtime_error);
}

TEST(TextEncodingTest, WideAboveMaxCodePointThrows) {
  // U+110000
  EXPECT_THROW(utf8ToWide("\xF4\x90\x80\x80"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xF7\xBF\xBF\xBF"), std::runtime_error);
  // 5- and 6-byte lead bytes, and the never-valid 0xFE / 0xFF.
  EXPECT_THROW(utf8ToWide("\xF8\x88\x80\x80\x80"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xFC\x84\x80\x80\x80\x80"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xFE"), std::runtime_error);
  EXPECT_THROW(utf8ToWide("\xFF"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// resolveAnsiEncoding / utf8ToAnsi / calculateAnsiSafeCopyLength /
// calculateAnsiChunkLength
// ---------------------------------------------------------------------------

namespace {

// Reads value in chunks of at most chunkBytes, the way chunked SQLGetData
// does, and returns the chunks.
std::vector<std::string> readInChunks(const std::string& value,
                                      SQLLEN chunkBytes,
                                      AnsiEncoding encoding) {
  std::vector<std::string> chunks;
  std::size_t offset = 0;
  while (offset < value.size()) {
    const SQLLEN length =
        calculateAnsiChunkLength(value, offset, chunkBytes, encoding);
    EXPECT_GT(length, 0) << "no progress at offset " << offset;
    if (length <= 0) {
      break;
    }
    chunks.push_back(value.substr(offset, static_cast<std::size_t>(length)));
    offset += static_cast<std::size_t>(length);
  }
  return chunks;
}

std::string join(const std::vector<std::string>& parts) {
  std::string result;
  for (const std::string& part : parts) {
    result += part;
  }
  return result;
}

} // namespace

TEST(AnsiEncodingTest, Utf8IsPassThrough) {
  const AnsiEncoding utf8 = resolveAnsiEncoding("UTF-8");
  EXPECT_EQ(utf8, utf8AnsiEncoding());
  EXPECT_EQ(resolveAnsiEncoding("utf8"), utf8);
  EXPECT_EQ(resolveAnsiEncoding("  utf-8 "), utf8);

  bool replaced = true;
  EXPECT_EQ(utf8ToAnsi("", utf8, &replaced), "");
  EXPECT_FALSE(replaced);
  EXPECT_EQ(utf8ToAnsi("plain", utf8), "plain");
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, utf8), CYRILLIC_UTF8);
  std::string mixed = "a" + CYRILLIC_UTF8 + CJK_UTF8 + EMOJI_UTF8;
  EXPECT_EQ(utf8ToAnsi(mixed, utf8, &replaced), mixed);
  EXPECT_FALSE(replaced);
}

TEST(AnsiEncodingTest, Utf8SafeCopyMatchesUtf8) {
  const AnsiEncoding utf8 = utf8AnsiEncoding();
  std::string value       = "a" + CYRILLIC_UTF8 + CJK_UTF8 + EMOJI_UTF8;
  for (std::size_t offset = 0; offset <= value.size(); offset++) {
    for (SQLLEN max = 0; max <= static_cast<SQLLEN>(value.size()) + 1; max++) {
      EXPECT_EQ(calculateAnsiSafeCopyLength(value, offset, max, utf8),
                calculateUtf8SafeCopyLength(value, offset, max))
          << "offset=" << offset << " max=" << max;
    }
  }
}

TEST(AnsiEncodingTest, SafeCopyAscii) {
  // Plain ASCII is single-byte in every supported encoding.
  const AnsiEncoding encoding = resolveAnsiEncoding("auto");
  std::string value           = "abcdef";
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 4, encoding),
            static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 4, 4, encoding),
            static_cast<SQLLEN>(2));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 0, encoding),
            static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 6, 4, encoding),
            static_cast<SQLLEN>(0));
}

TEST(AnsiEncodingTest, RejectsUnknownEncoding) {
  EXPECT_THROW(resolveAnsiEncoding("no-such-charset"), std::invalid_argument);
  EXPECT_THROW(resolveAnsiEncoding("system"), std::invalid_argument);
  try {
    resolveAnsiEncoding("  no-such-charset ");
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    // The message names the option and the trimmed value.
    EXPECT_NE(
        std::string(e.what()).find("ansiEncoding value 'no-such-charset'"),
        std::string::npos)
        << e.what();
  }
}

TEST(AnsiEncodingTest, AutoIsTheDefault) {
  const AnsiEncoding automatic = resolveAnsiEncoding("auto");
  EXPECT_EQ(resolveAnsiEncoding(""), automatic);
  EXPECT_EQ(resolveAnsiEncoding(" AUTO "), automatic);
  EXPECT_EQ(utf8ToAnsi("plain ASCII", automatic), "plain ASCII");
  EXPECT_FALSE(ansiEncodingName(automatic).empty());
#ifdef __APPLE__
  // The test binary is not a Microsoft Office application.
  EXPECT_EQ(automatic, utf8AnsiEncoding());
#endif
}

TEST(AnsiEncodingTest, Utf8ChunksMakeProgressWithTinyBuffers) {
  const AnsiEncoding utf8 = utf8AnsiEncoding();
  const std::string value = "a" + CYRILLIC_UTF8 + EMOJI_UTF8;
  // Smaller than one character: the character is split, never stalls.
  EXPECT_EQ(calculateAnsiSafeCopyLength(EMOJI_UTF8, 0, 3, utf8), 0);
  EXPECT_EQ(calculateAnsiChunkLength(EMOJI_UTF8, 0, 3, utf8), 3);
  EXPECT_EQ(calculateAnsiChunkLength(EMOJI_UTF8, 3, 3, utf8), 1);
  EXPECT_EQ(calculateAnsiChunkLength(EMOJI_UTF8, 4, 3, utf8), 0);
  EXPECT_EQ(calculateAnsiChunkLength(EMOJI_UTF8, 0, 0, utf8), 0);
  for (SQLLEN chunk = 1; chunk <= 8; chunk++) {
    EXPECT_EQ(join(readInChunks(value, chunk, utf8)), value) << chunk;
  }
}

#ifdef _WIN32
TEST(AnsiEncodingTest, WindowsTakesOnlyAutoAndUtf8) {
  EXPECT_THROW(resolveAnsiEncoding("windows-1251"), std::invalid_argument);
  EXPECT_THROW(resolveAnsiEncoding("x-mac-cyrillic"), std::invalid_argument);
  EXPECT_EQ(resolveAnsiEncoding("auto").id,
            static_cast<std::uint32_t>(GetACP()));
}
#endif

#ifdef __APPLE__
// Any ASCII-compatible IANA charset can be selected on macOS.

TEST(AnsiEncodingTest, MacRoman) {
  const AnsiEncoding encoding = resolveAnsiEncoding("macintosh");
  bool replaced               = true;
  // "Café" -> é is 0x8E in Mac Roman.
  EXPECT_EQ(utf8ToAnsi("Caf\xC3\xA9", encoding, &replaced), "Caf\x8E");
  EXPECT_FALSE(replaced);
  // No Cyrillic in Mac Roman: every letter becomes '?'.
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, encoding, &replaced), "??????, ???");
  EXPECT_TRUE(replaced);
  // A '?' in the data is not a replacement.
  EXPECT_EQ(utf8ToAnsi("why?", encoding, &replaced), "why?");
  EXPECT_FALSE(replaced);
}

TEST(AnsiEncodingTest, Windows1251) {
  const AnsiEncoding encoding = resolveAnsiEncoding("windows-1251");
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, encoding),
            "\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0");
  EXPECT_EQ(ansiEncodingName(encoding), "windows-1251");
}

TEST(AnsiEncodingTest, MacCyrillic) {
  const AnsiEncoding encoding = resolveAnsiEncoding("x-mac-cyrillic");
  // П is 0x8F, р is 0xF0 in Mac Cyrillic.
  EXPECT_EQ(utf8ToAnsi("\xD0\x9F\xD1\x80", encoding), "\x8F\xF0");
}

TEST(AnsiEncodingTest, EncodingsAreIndependentValues) {
  // Two connections with different encodings: resolving one doesn't change
  // conversions with the other.
  const AnsiEncoding cp1251 = resolveAnsiEncoding("windows-1251");
  const AnsiEncoding utf8   = resolveAnsiEncoding("UTF-8");
  const AnsiEncoding roman  = resolveAnsiEncoding("macintosh");
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, utf8), CYRILLIC_UTF8);
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, roman), "??????, ???");
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, cp1251),
            "\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0");
  EXPECT_EQ(utf8ToAnsi(CYRILLIC_UTF8, utf8), CYRILLIC_UTF8);
}

TEST(AnsiEncodingTest, RejectsNonAsciiCompatibleEncodings) {
  for (const char* name : {"UTF-16",
                           "UTF-16LE",
                           "UTF-16BE",
                           "UTF-32",
                           "UTF-32LE",
                           "UTF-7",
                           "IBM037",
                           "ISO-2022-JP",
                           "ISO-2022-KR",
                           "HZ-GB-2312"}) {
    EXPECT_THROW(resolveAnsiEncoding(name), std::invalid_argument) << name;
  }
  for (const char* name : {"macintosh",
                           "x-mac-cyrillic",
                           "windows-1251",
                           "ISO-8859-5",
                           "KOI8-R",
                           "Shift_JIS",
                           "EUC-JP",
                           "GB18030",
                           "Big5"}) {
    EXPECT_NO_THROW(resolveAnsiEncoding(name)) << name;
  }
}

TEST(AnsiEncodingTest, SingleByteSafeCopyIsPlainLength) {
  const AnsiEncoding encoding = resolveAnsiEncoding("windows-1251");
  const std::string value     = utf8ToAnsi(CYRILLIC_UTF8, encoding);
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 5, encoding),
            static_cast<SQLLEN>(5));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 9, 5, encoding),
            static_cast<SQLLEN>(2));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 0, encoding),
            static_cast<SQLLEN>(0));
  // Chunked reads of non-ASCII text in a legacy encoding.
  for (SQLLEN chunk = 1; chunk <= 4; chunk++) {
    const std::vector<std::string> chunks =
        readInChunks(value, chunk, encoding);
    EXPECT_EQ(join(chunks), value) << chunk;
    EXPECT_EQ(chunks.size(), (value.size() + chunk - 1) / chunk) << chunk;
  }
}

TEST(AnsiEncodingTest, MultiByteSafeCopyNeverSplitsACharacter) {
  const AnsiEncoding encoding = resolveAnsiEncoding("Shift_JIS");
  // "a中文" -> 'a' + two 2-byte characters.
  const std::string value = utf8ToAnsi("a" + CJK_UTF8, encoding);
  ASSERT_EQ(value.size(), 5U);
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 2, encoding),
            static_cast<SQLLEN>(1));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 3, encoding),
            static_cast<SQLLEN>(3));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 1, 3, encoding),
            static_cast<SQLLEN>(2));
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 1, 10, encoding),
            static_cast<SQLLEN>(4));
}

TEST(AnsiEncodingTest, Gb18030FourByteCharacters) {
  const AnsiEncoding encoding = resolveAnsiEncoding("GB18030");
  // U+1F600 is a 4-byte GB18030 sequence; 中 is 2 bytes.
  const std::string emoji = utf8ToAnsi(EMOJI_UTF8, encoding);
  ASSERT_EQ(emoji.size(), 4U);
  const std::string value = "a" + emoji + utf8ToAnsi(CJK_UTF8, encoding);
  ASSERT_EQ(value.size(), 9U);

  // A chunk never ends inside the 4-byte character...
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 4, encoding), 1);
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 0, 5, encoding), 5);
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 1, 6, encoding), 6);
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 1, 5, encoding), 4);
  // ...unless the buffer is smaller than the character.
  EXPECT_EQ(calculateAnsiSafeCopyLength(value, 1, 3, encoding), 0);
  EXPECT_EQ(calculateAnsiChunkLength(value, 1, 3, encoding), 3);

  for (SQLLEN chunk = 1; chunk <= 10; chunk++) {
    const std::vector<std::string> chunks =
        readInChunks(value, chunk, encoding);
    EXPECT_EQ(join(chunks), value) << chunk;
    if (chunk >= 4) {
      for (const std::string& part : chunks) {
        EXPECT_EQ(calculateAnsiSafeCopyLength(
                      part, 0, static_cast<SQLLEN>(part.size()), encoding),
                  static_cast<SQLLEN>(part.size()))
            << "chunk " << chunk << " split a character";
      }
    }
  }
}

TEST(AnsiEncodingTest, OfficeMacCodePages) {
  EXPECT_EQ(macCodePageForLanguage("en"), 10000U);
  EXPECT_EQ(macCodePageForLanguage("en-GB"), 10000U);
  EXPECT_EQ(macCodePageForLanguage(""), 10000U);
  EXPECT_EQ(macCodePageForLanguage("ru"), 10007U);
  EXPECT_EQ(macCodePageForLanguage("ru-RU"), 10007U);
  EXPECT_EQ(macCodePageForLanguage("ru_RU"), 10007U);
  EXPECT_EQ(macCodePageForLanguage("uk"), 10017U);
  EXPECT_EQ(macCodePageForLanguage("pl"), 10029U);
  EXPECT_EQ(macCodePageForLanguage("sr-Latn"), 10082U);
  EXPECT_EQ(macCodePageForLanguage("sr-Cyrl"), 10007U);
  EXPECT_EQ(macCodePageForLanguage("ja"), 10001U);
  EXPECT_EQ(macCodePageForLanguage("zh-Hans"), 10008U);
  EXPECT_EQ(macCodePageForLanguage("zh-Hant"), 10002U);
  EXPECT_EQ(macCodePageForLanguage("zh_TW"), 10002U);
  EXPECT_EQ(macCodePageForLanguage("tr"), 10081U);

  EXPECT_TRUE(isMicrosoftOfficeBundle("com.microsoft.Excel"));
  EXPECT_TRUE(isMicrosoftOfficeBundle("com.microsoft.Word"));
  EXPECT_TRUE(isMicrosoftOfficeBundle("com.microsoft.Powerpoint"));
  EXPECT_FALSE(isMicrosoftOfficeBundle("com.microsoft.VSCode"));
  EXPECT_FALSE(isMicrosoftOfficeBundle("com.microsoft.edgemac"));
  EXPECT_FALSE(isMicrosoftOfficeBundle("com.microsoft.teams2"));
  EXPECT_FALSE(isMicrosoftOfficeBundle("com.apple.Terminal"));
  EXPECT_FALSE(isMicrosoftOfficeBundle(""));
}

TEST(AnsiEncodingTest, AutoFollowsOfficeUiLanguage) {
  // Verified in Excel 16.115: English UI -> Mac Roman, Russian -> Mac
  // Cyrillic.
  EXPECT_EQ(autoAnsiEncoding("com.microsoft.Excel", "en"),
            resolveAnsiEncoding("macintosh"));
  EXPECT_EQ(autoAnsiEncoding("com.microsoft.Excel", "ru"),
            resolveAnsiEncoding("x-mac-cyrillic"));
  EXPECT_EQ(autoAnsiEncoding("com.microsoft.Excel", ""),
            resolveAnsiEncoding("macintosh"));
  // Other applications get UTF-8 whatever their language.
  EXPECT_EQ(autoAnsiEncoding("com.microsoft.VSCode", "ru"), utf8AnsiEncoding());
  EXPECT_EQ(autoAnsiEncoding("org.iodbc.iodbctest", "ru"), utf8AnsiEncoding());
  EXPECT_EQ(autoAnsiEncoding("", "en"), utf8AnsiEncoding());

  // Every encoding "auto" can pick is one an explicit ansiEncoding accepts.
  for (const char* language : {"en",
                               "ru",
                               "uk",
                               "pl",
                               "hr",
                               "ro",
                               "el",
                               "tr",
                               "is",
                               "ja",
                               "ko",
                               "he",
                               "ar",
                               "th",
                               "zh-Hans",
                               "zh-Hant"}) {
    const AnsiEncoding encoding =
        autoAnsiEncoding("com.microsoft.Excel", language);
    EXPECT_NE(encoding, utf8AnsiEncoding()) << language;
    EXPECT_NO_THROW(
        EXPECT_EQ(resolveAnsiEncoding(ansiEncodingName(encoding)), encoding))
        << language << " " << ansiEncodingName(encoding);
  }
}

TEST(AnsiEncodingTest, BidiMacEncodingsAreAsciiCompatible) {
  // Core Foundation decodes ASCII in these with directional marks around it.
  for (const char* name : {"x-mac-hebrew", "x-mac-arabic"}) {
    AnsiEncoding encoding = utf8AnsiEncoding();
    ASSERT_NO_THROW(encoding = resolveAnsiEncoding(name)) << name;
    EXPECT_EQ(utf8ToAnsi("SELECT 1", encoding), "SELECT 1") << name;
  }
  // Shalom: SHIN LAMED VAV FINAL-MEM is F9 EC E5 ED in Mac Hebrew.
  EXPECT_EQ(utf8ToAnsi("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",
                       resolveAnsiEncoding("x-mac-hebrew")),
            "\xF9\xEC\xE5\xED");
}
#endif

// ---------------------------------------------------------------------------
// calculateUtf8SafeCopyLength
// ---------------------------------------------------------------------------

TEST(TextEncodingTest, Utf8SafeCopyDegenerate) {
  EXPECT_EQ(calculateUtf8SafeCopyLength("", 0, 10), static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateUtf8SafeCopyLength("abc", 0, 0), static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateUtf8SafeCopyLength("abc", 0, -1), static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateUtf8SafeCopyLength("abc", 3, 10), static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateUtf8SafeCopyLength("abc", 99, 10), static_cast<SQLLEN>(0));
}

TEST(TextEncodingTest, Utf8SafeCopyAscii) {
  EXPECT_EQ(calculateUtf8SafeCopyLength("abcdef", 0, 4),
            static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateUtf8SafeCopyLength("abcdef", 2, 100),
            static_cast<SQLLEN>(4));
}

TEST(TextEncodingTest, Utf8SafeCopyCyrillic) {
  // Every Cyrillic letter is 2 bytes; "Привет" is 12 bytes.
  EXPECT_EQ(calculateUtf8SafeCopyLength(CYRILLIC_UTF8, 0, 5),
            static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateUtf8SafeCopyLength(CYRILLIC_UTF8, 0, 6),
            static_cast<SQLLEN>(6));
  EXPECT_EQ(calculateUtf8SafeCopyLength(CYRILLIC_UTF8, 2, 3),
            static_cast<SQLLEN>(2));
  // Only one byte of room: nothing can be copied without splitting.
  EXPECT_EQ(calculateUtf8SafeCopyLength(CYRILLIC_UTF8, 0, 1),
            static_cast<SQLLEN>(0));
  // Whole string fits.
  EXPECT_EQ(calculateUtf8SafeCopyLength(CYRILLIC_UTF8, 0, 1000),
            static_cast<SQLLEN>(CYRILLIC_UTF8.size()));
}

TEST(TextEncodingTest, Utf8SafeCopyThreeAndFourByte) {
  // "a" + 中 (3) + 😀 (4) + "b"
  std::string value = "a" + std::string("\xE4\xB8\xAD") + EMOJI_UTF8 + "b";
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 1), static_cast<SQLLEN>(1));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 2), static_cast<SQLLEN>(1));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 3), static_cast<SQLLEN>(1));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 4), static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 7), static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 0, 8), static_cast<SQLLEN>(8));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 4, 3), static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 4, 4), static_cast<SQLLEN>(4));
  EXPECT_EQ(calculateUtf8SafeCopyLength(value, 4, 5), static_cast<SQLLEN>(5));
}

TEST(TextEncodingTest, Utf8ChunksNeverSplitCharacters) {
  const std::string value =
      "x" + CYRILLIC_UTF8 + CJK_UTF8 + EMOJI_UTF8 + "y" + EMOJI_UTF8;
  // A chunk of >= 4 bytes always makes progress.
  for (SQLLEN chunk = 4; chunk <= 13; chunk++) {
    std::string reassembled;
    std::size_t offset = 0;
    while (offset < value.size()) {
      SQLLEN len = calculateUtf8SafeCopyLength(value, offset, chunk);
      ASSERT_GT(len, 0) << "no progress at offset " << offset;
      ASSERT_LE(len, chunk);
      std::string piece = value.substr(offset, static_cast<std::size_t>(len));
      EXPECT_NO_THROW(utf8ToWide(piece))
          << "chunk " << chunk << " split a character at offset " << offset;
      reassembled += piece;
      offset += static_cast<std::size_t>(len);
    }
    EXPECT_EQ(reassembled, value) << "chunk " << chunk;
  }
}

// ---------------------------------------------------------------------------
// calculateWideSafeCopyLengthBytes
// ---------------------------------------------------------------------------

TEST(TextEncodingTest, WideSafeCopyDegenerate) {
  const std::wstring value = L"abc";
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(L"", 0, 100),
            static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, 0),
            static_cast<SQLLEN>(0));
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, -4),
            static_cast<SQLLEN>(0));
  // Less than one code unit of room.
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, unitBytes(1) - 1),
            static_cast<SQLLEN>(0));
  // Offset not aligned to a code unit.
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 1, 100),
            static_cast<SQLLEN>(0));
  // Offset past the end.
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, unitBytes(3), 100),
            static_cast<SQLLEN>(0));
}

TEST(TextEncodingTest, WideSafeCopyAsciiAndCyrillic) {
  const std::wstring value = CYRILLIC_WIDE; // 11 code units
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, unitBytes(4)),
            unitBytes(4));
  // A partial code unit of room is rounded down.
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, unitBytes(4) + 1),
            unitBytes(4));
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, unitBytes(8), 1000),
            unitBytes(3));
  EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, 100000), unitBytes(11));
}

TEST(TextEncodingTest, WideSafeCopyDoesNotSplitSurrogatePair) {
  const std::wstring value = utf8ToWide("a" + EMOJI_UTF8 + "b");
  // Room for two code units after "a".
  SQLLEN copied = calculateWideSafeCopyLengthBytes(value, 0, unitBytes(2));
  if constexpr (WIDE_IS_UTF16) {
    // "a", high surrogate, low surrogate, "b": copying 2 units would split
    // the pair, so only "a" is copied.
    EXPECT_EQ(copied, unitBytes(1));
    EXPECT_EQ(calculateWideSafeCopyLengthBytes(value, 0, unitBytes(3)),
              unitBytes(3));
  } else {
    // UTF-32: "a" and the emoji are one unit each.
    EXPECT_EQ(copied, unitBytes(2));
  }
}

TEST(TextEncodingTest, WideChunksNeverSplitCharacters) {
  const std::wstring value =
      utf8ToWide("x" + CYRILLIC_UTF8 + EMOJI_UTF8 + EMOJI_UTF8 + CJK_UTF8 +
                 "y" + EMOJI_UTF8);
  // A chunk of >= 2 code units always makes progress.
  for (std::size_t units = 2; units <= 7; units++) {
    std::wstring reassembled;
    std::size_t offsetBytes      = 0;
    const std::size_t totalBytes = value.size() * sizeof(SQLWCHAR);
    while (offsetBytes < totalBytes) {
      SQLLEN len = calculateWideSafeCopyLengthBytes(
          value, offsetBytes, unitBytes(units));
      ASSERT_GT(len, 0) << "no progress at byte offset " << offsetBytes;
      ASSERT_EQ(len % static_cast<SQLLEN>(sizeof(SQLWCHAR)), 0);
      const std::size_t start = offsetBytes / sizeof(SQLWCHAR);
      const std::size_t count =
          static_cast<std::size_t>(len) / sizeof(SQLWCHAR);
      std::wstring piece = value.substr(start, count);
      if constexpr (WIDE_IS_UTF16) {
        const unsigned last = static_cast<unsigned>(piece.back()) & 0xFFFFu;
        EXPECT_FALSE(last >= 0xD800u && last <= 0xDBFFu)
            << "chunk of " << units << " units ends in a high surrogate";
      }
      reassembled += piece;
      offsetBytes += static_cast<std::size_t>(len);
    }
    EXPECT_EQ(reassembled, value) << "chunk of " << units << " units";
  }
}
