#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/*
Text conversions between Trino's UTF-8 and the encodings ODBC applications
expect. Implementations live in src/platform/<os>/textEncoding.cpp (and
src/platform/mac/ansiEncoding.cpp).
*/

// UTF-8 to the SQLWCHAR representation: UTF-16 on Windows, UTF-32 with
// iODBC on macOS. Throws std::runtime_error on invalid UTF-8.
std::wstring utf8ToWide(const std::string& value);

// The narrow encoding of SQL_C_CHAR result data on one connection: what its
// ansiEncoding option resolved to. The id is platform-specific (a Windows
// code page, a macOS CFStringEncoding); only compare it. There is no default:
// get one from resolveAnsiEncoding or utf8AnsiEncoding.
struct AnsiEncoding {
    explicit AnsiEncoding(std::uint32_t value) : id(value) {}

    std::uint32_t id;

    bool operator==(const AnsiEncoding& other) const = default;
};

// UTF-8: no conversion. Every platform supports it.
AnsiEncoding utf8AnsiEncoding();

// Resolves the ansiEncoding connection option (surrounding spaces and case
// are ignored):
//   "auto" (or empty, the default): what the host application decodes
//          SQL_C_CHAR with. Windows: the ANSI code page. macOS: Excel, Word
//          and PowerPoint use the Mac encoding of their UI language (Mac
//          Roman for English, Mac Cyrillic for Russian, ...); other apps get
//          UTF-8.
//   "UTF-8": no conversion;
//   macOS also takes an IANA charset name, e.g. "x-mac-cyrillic".
// Only ASCII-compatible encodings are accepted (bytes 0x01-0x7F mean ASCII),
// so not UTF-16/32, UTF-7, EBCDIC, ISO-2022 or HZ. JIS-Roman encodings
// (Shift_JIS, Mac Japanese) show 0x5C as a yen sign: a backslash in the data
// becomes a fullwidth backslash and a tilde becomes '?'.
//
// The encoding applies to SQL_C_CHAR data: results (SQLGetData, SQLBindCol)
// and parameters (SQLBindParameter). With iODBC (macOS), a non-UTF-8
// encoding also applies to the strings of the ODBC calls (SQL text, names,
// diagnostics): iODBC hands an ANSI application's calls to the driver's W
// functions byte by byte (see WideTextCodec in
// src/driver/unicode/wideText.hpp). So a non-UTF-8 ansiEncoding is for ANSI
// applications; Unicode (W) applications should keep UTF-8, which "auto"
// gives them. The driver's own narrow (A) functions always take UTF-8.
//
// Throws std::invalid_argument for an unknown or unsupported name. No side
// effects.
AnsiEncoding resolveAnsiEncoding(const std::string& name);

// A readable name of a resolved encoding, for logs.
std::string ansiEncodingName(AnsiEncoding encoding);

// UTF-8 to the given narrow encoding. Characters it can't represent become
// '?'; *replaced (when given) says whether that happened. Throws
// std::runtime_error on invalid UTF-8.
std::string utf8ToAnsi(const std::string& value,
                       AnsiEncoding encoding,
                       bool* replaced = nullptr);

// The other way: bytes in the given narrow encoding to UTF-8, or nullopt when
// they aren't valid in it.
std::optional<std::string> ansiToUtf8(std::string_view bytes,
                                      AnsiEncoding encoding);

// Wide string to UTF-8, e.g. for logging. Invalid code units are replaced
// with U+FFFD; never throws on bad input.
std::string wideToUtf8(const std::wstring& value);
