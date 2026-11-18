#pragma once

#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "../../platform/textEncoding.hpp"

/*
Conversions between the driver's internal UTF-8 strings and the SQLWCHAR
strings of the ODBC Unicode ("W") API.

SQLWCHAR holds UTF-16 when it's 2 bytes (Windows, unixODBC) and UTF-32 when
it's 4 bytes (iODBC on macOS, where SQLWCHAR is wchar_t). The encoding is
chosen by sizeof(SQLWCHAR) alone, not by wchar_t.

The template functions work on char16_t / char32_t, so both encodings can be
tested on any platform. None of these functions throw on invalid input:
malformed sequences are replaced with U+FFFD.
*/

static_assert(sizeof(SQLWCHAR) == 2 || sizeof(SQLWCHAR) == 4,
              "SQLWCHAR must be UTF-16 (2 bytes) or UTF-32 (4 bytes)");

// The code unit type with the same size and encoding as SQLWCHAR.
using SqlWideUnit =
    std::conditional_t<sizeof(SQLWCHAR) == 2, char16_t, char32_t>;
using SqlWideString = std::basic_string<SqlWideUnit>;

// UTF-8 to UTF-16 (char16_t) or UTF-32 (char32_t).
template <typename Unit>
std::basic_string<Unit> utf8ToUtf(std::string_view utf8);

// UTF-16 / UTF-32 to UTF-8. Unpaired surrogates and values above U+10FFFF
// become U+FFFD.
template <typename Unit>
std::string utfToUtf8(std::basic_string_view<Unit> text);

// The largest prefix length <= maxUnits that doesn't split a character,
// i.e. doesn't end between the two halves of a UTF-16 surrogate pair.
template <typename Unit>
std::size_t utfCharBoundary(std::basic_string_view<Unit> text,
                            std::size_t maxUnits);

/*
How the W functions read and write SQLWCHAR text on one connection.

Normally it's Unicode. But iODBC turns the narrow (A) calls of an ANSI
application such as Excel for Mac into W calls with mbrtowc in the
application's locale, which is "C" in Cocoa apps: every byte becomes one
SQLWCHAR (0x00-0xFF), whatever its encoding. W output goes back through
wcrtomb, so anything above U+00FF reaches the application as '?'. Text
of such an application is its narrow encoding (ansiEncoding), one byte per
SQLWCHAR: widenedAnsi.
*/
struct WideTextCodec {
    // Set: the application's narrow text, widened byte by byte.
    std::optional<AnsiEncoding> widenedAnsi;
};

// The codec for a connection's ansiEncoding: widened when the encoding is a
// legacy one (not UTF-8) and the driver manager is iODBC (4-byte SQLWCHAR;
// Windows and unixODBC convert narrow calls properly).
WideTextCodec wideTextCodecFor(AnsiEncoding ansiEncoding);

// Widened narrow text to UTF-8: when every unit is 0x00-0xFF and some are
// 0x80 or above, the units are bytes in the encoding. Otherwise (plain
// ASCII, real Unicode text from a W application, or bytes invalid in the
// encoding) nullopt: read it as Unicode.
template <typename Unit>
std::optional<std::string> widenedAnsiToUtf8(std::basic_string_view<Unit> text,
                                             AnsiEncoding encoding);

// UTF-8 to widened narrow text: converted to the encoding ('?' for what it
// can't represent), one unit per byte.
template <typename Unit>
std::basic_string<Unit> utf8ToWidenedAnsi(std::string_view utf8,
                                          AnsiEncoding encoding);

// Number of SQLWCHARs before the terminating NUL.
std::size_t sqlWideLength(const SQLWCHAR* text);

// Application input: a SQLWCHAR string whose length is given in characters,
// or SQL_NTS for a NUL-terminated string. A null pointer gives "".
std::string sqlWideToUtf8(const SQLWCHAR* text,
                          SQLLEN lengthChars,
                          const WideTextCodec& codec = {});

SqlWideString utf8ToSqlWide(std::string_view utf8,
                            const WideTextCodec& codec = {});

struct WideCopyResult {
    // Length of the complete string in SQLWCHARs, without the terminator.
    std::size_t totalUnits = 0;
    // True when the output buffer couldn't hold the complete string plus
    // its terminator.
    bool truncated = false;
};

/*
Copies text into an output buffer of capacityUnits SQLWCHARs, terminator
included. The copy is cut on a character boundary (of the narrow encoding
for widened text) and NUL-terminated
whenever there is room for the terminator. A null buffer only reports the
length and is never truncated.
*/
WideCopyResult copyToSqlWideBuffer(const SqlWideString& text,
                                   SQLWCHAR* buffer,
                                   std::size_t capacityUnits,
                                   const WideTextCodec& codec = {});

// What iODBC's SQL_ATTR_DRIVER_UNICODE_TYPE (SQLGetEnvAttr) expects: the
// encoding of the driver's SQLWCHAR strings (SQL_DM_CP_UTF16 = 1,
// SQL_DM_CP_UCS4 = 3, see iodbcext.h).
constexpr SQLINTEGER TRINO_SQL_ATTR_DRIVER_UNICODE_TYPE = 1065;
constexpr SQLINTEGER TRINO_SQL_DM_CP_UTF16              = 1;
constexpr SQLINTEGER TRINO_SQL_DM_CP_UCS4               = 3;

constexpr SQLINTEGER driverUnicodeType() {
  return sizeof(SQLWCHAR) == 2 ? TRINO_SQL_DM_CP_UTF16 : TRINO_SQL_DM_CP_UCS4;
}
