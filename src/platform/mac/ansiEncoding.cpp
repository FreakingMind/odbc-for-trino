#include "ansiEncoding.hpp"

#include "../textEncoding.hpp"

#include "../../util/rowToBuffer.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../util/stringTrim.hpp"

/*
The narrow ("ANSI") encoding of SQL_C_CHAR data on macOS.

Excel for Mac (Microsoft Query) is an ANSI ODBC client: it fetches all text
as SQL_C_CHAR and sends SQL in the same encoding. Verified with Excel 16.115:
that encoding is the Mac encoding of Excel's UI language (Mac Roman for
English, Mac Cyrillic for Russian), not the system encoding
(__CF_USER_TEXT_ENCODING / CFStringGetSystemEncoding) and not UTF-8. The UI
language follows the per-app language setting (System Settings > Language &
Region > Applications), so Russian text works without changing the system
language. Office for Windows maps languages to "Mac code pages" with the same
table (LOCALE_IDEFAULTMACCODEPAGE); macCodePageForLanguage repeats it.

Other applications (iodbctest, scripts, BI tools) get UTF-8, as on any other
POSIX system. Any IANA charset can be selected with the ansiEncoding option.
*/

namespace {

// CFStringCreateWithBytes / CFStringGetBytes: no byte order mark.
constexpr Boolean NO_EXTERNAL_REPRESENTATION = 0;

// A character takes at most 4 bytes in every supported multi-byte encoding
// (GB18030 is the longest).
constexpr std::size_t MAX_CHAR_BYTES = 4;

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return value;
}

// Owns a Core Foundation object.
template <typename Ref> class CFHolder {
  public:
    explicit CFHolder(Ref ref) : ref_(ref) {}
    ~CFHolder() {
      if (ref_) {
        CFRelease(ref_);
      }
    }
    CFHolder(const CFHolder&)            = delete;
    CFHolder& operator=(const CFHolder&) = delete;

    Ref get() const {
      return ref_;
    }

  private:
    Ref ref_;
};

CFHolder<CFStringRef>
fromBytes(const char* data, std::size_t size, CFStringEncoding encoding) {
  return CFHolder<CFStringRef>(
      CFStringCreateWithBytes(kCFAllocatorDefault,
                              reinterpret_cast<const UInt8*>(data),
                              static_cast<CFIndex>(size),
                              encoding,
                              NO_EXTERNAL_REPRESENTATION));
}

// Converts a CFString to bytes in the given encoding, '?' for unmappable
// characters. Sets *replaced when the lossy conversion changed something.
std::string
toBytes(CFStringRef text, CFStringEncoding encoding, bool* replaced) {
  const CFRange range           = CFRangeMake(0, CFStringGetLength(text));
  CFIndex required              = 0;
  const CFIndex convertedStrict = CFStringGetBytes(text,
                                                   range,
                                                   encoding,
                                                   0,
                                                   NO_EXTERNAL_REPRESENTATION,
                                                   nullptr,
                                                   0,
                                                   nullptr);
  if (replaced != nullptr) {
    *replaced = convertedStrict != range.length;
  }

  CFStringGetBytes(text,
                   range,
                   encoding,
                   '?',
                   NO_EXTERNAL_REPRESENTATION,
                   nullptr,
                   0,
                   &required);
  std::string result(static_cast<std::size_t>(required), '\0');
  CFIndex written = 0;
  CFStringGetBytes(text,
                   range,
                   encoding,
                   '?',
                   NO_EXTERNAL_REPRESENTATION,
                   reinterpret_cast<UInt8*>(result.data()),
                   required,
                   &written);
  result.resize(static_cast<std::size_t>(written));
  return result;
}

std::string cfStringToUtf8(CFStringRef text) {
  if (text == nullptr) {
    return {};
  }
  return toBytes(text, kCFStringEncodingUTF8, nullptr);
}

// Stateful encodings: escape sequences (ESC, SO/SI, '~{', '+') switch the
// meaning of the following ASCII bytes.
bool isStateful(CFStringEncoding encoding) {
  switch (encoding) {
    case kCFStringEncodingUTF7:
    case kCFStringEncodingUTF7_IMAP:
    case kCFStringEncodingISO_2022_JP:
    case kCFStringEncodingISO_2022_JP_1:
    case kCFStringEncodingISO_2022_JP_2:
    case kCFStringEncodingISO_2022_JP_3:
    case kCFStringEncodingISO_2022_CN:
    case kCFStringEncodingISO_2022_CN_EXT:
    case kCFStringEncodingISO_2022_KR:
    case kCFStringEncodingHZ_GB_2312:
      return true;
    default:
      return false;
  }
}

// Directional formatting marks: CF adds them around runs of ASCII decoded
// from Mac Arabic and Mac Hebrew. They aren't in the bytes.
bool isBidiMark(UniChar character) {
  return character == 0x200E || character == 0x200F ||
         (character >= 0x202A && character <= 0x202E);
}

// Each byte 0x01-0x7F must decode to one ASCII character (JIS-Roman
// encodings such as Shift_JIS and Mac Japanese may show 0x5C as a yen sign
// and 0x7E as an overline; Mac Arabic and Hebrew add bidi marks) and convert
// back to the same byte. Rules out UTF-16/32 (no 1-byte units), EBCDIC and
// the stateful encodings: the driver copies, counts and splits narrow text as
// bytes, and applications parse it as ASCII-based.
bool isAsciiCompatible(CFStringEncoding encoding) {
  if (isStateful(encoding)) {
    return false;
  }

  std::string ascii;
  for (int c = 0x01; c <= 0x7F; c++) {
    ascii.push_back(static_cast<char>(c));
  }

  const CFHolder<CFStringRef> decoded =
      fromBytes(ascii.data(), ascii.size(), encoding);
  if (decoded.get() == nullptr) {
    return false;
  }
  std::size_t next     = 0;
  const CFIndex length = CFStringGetLength(decoded.get());
  for (CFIndex i = 0; i < length; i++) {
    const UniChar character = CFStringGetCharacterAtIndex(decoded.get(), i);
    if (isBidiMark(character)) {
      continue;
    }
    if (next >= ascii.size()) {
      return false;
    }
    const auto byte =
        static_cast<UniChar>(static_cast<unsigned char>(ascii[next++]));
    const bool jisRoman = (byte == 0x5C || byte == 0x7E) && character > 0x7F;
    if (character != byte && !jisRoman) {
      return false;
    }
  }
  if (next != ascii.size()) {
    return false;
  }

  bool replaced = false;
  return toBytes(decoded.get(), encoding, &replaced) == ascii && !replaced;
}

std::string mainBundleIdentifier() {
  CFBundleRef bundle = CFBundleGetMainBundle();
  if (bundle == nullptr) {
    return {};
  }
  // Get rule: not owned.
  return cfStringToUtf8(CFBundleGetIdentifier(bundle));
}

// The language the host application shows its UI in: the best match between
// its localizations and the user's (or the per-app) language preferences.
std::string mainBundleUiLanguage() {
  CFBundleRef bundle = CFBundleGetMainBundle();
  if (bundle == nullptr) {
    return {};
  }
  const CFHolder<CFArrayRef> localizations(
      CFBundleCopyBundleLocalizations(bundle));
  if (localizations.get() == nullptr ||
      CFArrayGetCount(localizations.get()) == 0) {
    return {};
  }
  const CFHolder<CFArrayRef> preferred(
      CFBundleCopyPreferredLocalizationsFromArray(localizations.get()));
  if (preferred.get() == nullptr || CFArrayGetCount(preferred.get()) == 0) {
    return {};
  }
  return cfStringToUtf8(
      static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred.get(), 0)));
}

AnsiEncoding toAnsiEncoding(CFStringEncoding encoding) {
  return AnsiEncoding{static_cast<std::uint32_t>(encoding)};
}

CFStringEncoding toCFEncoding(AnsiEncoding encoding) {
  return static_cast<CFStringEncoding>(encoding.id);
}

bool isSingleByte(CFStringEncoding encoding) {
  return CFStringGetMaximumSizeForEncoding(1, encoding) == 1;
}

} // namespace

std::uint32_t macCodePageForLanguage(const std::string& languageTag) {
  std::string tag = toLower(languageTag);
  std::replace(tag.begin(), tag.end(), '_', '-');
  const std::string language = tag.substr(0, tag.find('-'));

  if (language == "zh") {
    const bool traditional = tag.find("hant") != std::string::npos ||
                             tag.find("-tw") != std::string::npos ||
                             tag.find("-hk") != std::string::npos ||
                             tag.find("-mo") != std::string::npos;
    return traditional ? 10002 : 10008;
  }
  if (language == "sr") {
    return tag.find("latn") != std::string::npos ? 10082 : 10007;
  }
  if (language == "ru" || language == "be" || language == "bg" ||
      language == "mk" || language == "kk" || language == "ky" ||
      language == "tt" || language == "mn") {
    return 10007; // Mac Cyrillic
  }
  if (language == "uk") {
    return 10017; // Mac Ukrainian
  }
  if (language == "cs" || language == "hu" || language == "pl" ||
      language == "sk" || language == "sl" || language == "et" ||
      language == "lv" || language == "lt") {
    return 10029; // Mac Central European
  }
  if (language == "hr" || language == "bs") {
    return 10082; // Mac Croatian
  }
  if (language == "ro") {
    return 10010; // Mac Romanian
  }
  if (language == "el") {
    return 10006; // Mac Greek
  }
  if (language == "tr" || language == "az") {
    return 10081; // Mac Turkish
  }
  if (language == "is") {
    return 10079; // Mac Icelandic
  }
  if (language == "ja") {
    return 10001; // Mac Japanese
  }
  if (language == "ko") {
    return 10003; // Mac Korean
  }
  if (language == "he") {
    return 10005; // Mac Hebrew
  }
  if (language == "ar" || language == "fa" || language == "ur") {
    return 10004; // Mac Arabic
  }
  if (language == "th") {
    return 10021; // Mac Thai
  }
  return 10000; // Mac Roman
}

bool isMicrosoftOfficeBundle(const std::string& bundleIdentifier) {
  const std::string id = toLower(bundleIdentifier);
  return id == "com.microsoft.excel" || id == "com.microsoft.word" ||
         id == "com.microsoft.powerpoint";
}

AnsiEncoding autoAnsiEncoding(const std::string& bundleIdentifier,
                              const std::string& uiLanguage) {
  if (!isMicrosoftOfficeBundle(bundleIdentifier)) {
    return utf8AnsiEncoding();
  }
  const CFStringEncoding encoding = CFStringConvertWindowsCodepageToEncoding(
      macCodePageForLanguage(uiLanguage));
  return toAnsiEncoding(encoding == kCFStringEncodingInvalidId
                            ? kCFStringEncodingMacRoman
                            : encoding);
}

AnsiEncoding utf8AnsiEncoding() {
  return toAnsiEncoding(kCFStringEncodingUTF8);
}

AnsiEncoding resolveAnsiEncoding(const std::string& name) {
  std::string trimmed = name;
  trim(trimmed);
  const std::string lower = toLower(trimmed);
  if (lower.empty() || lower == "auto") {
    return autoAnsiEncoding(mainBundleIdentifier(), mainBundleUiLanguage());
  }

  const CFHolder<CFStringRef> cfName =
      fromBytes(trimmed.data(), trimmed.size(), kCFStringEncodingASCII);
  const CFStringEncoding encoding =
      cfName.get() != nullptr
          ? CFStringConvertIANACharSetNameToEncoding(cfName.get())
          : kCFStringEncodingInvalidId;
  if (encoding == kCFStringEncodingInvalidId ||
      CFStringIsEncodingAvailable(encoding) == 0) {
    throw std::invalid_argument("Invalid ansiEncoding value '" + trimmed +
                                "': use auto, UTF-8 or an IANA charset "
                                "name such as x-mac-cyrillic");
  }
  if (!isAsciiCompatible(encoding)) {
    throw std::invalid_argument("Unsupported ansiEncoding value '" + trimmed +
                                "': the encoding must be ASCII-compatible "
                                "(not UTF-16/32, UTF-7, EBCDIC, ISO-2022 "
                                "or HZ)");
  }
  return toAnsiEncoding(encoding);
}

std::string ansiEncodingName(AnsiEncoding encoding) {
  const CFStringRef name =
      CFStringConvertEncodingToIANACharSetName(toCFEncoding(encoding));
  const std::string result = cfStringToUtf8(name);
  return result.empty() ? "CFStringEncoding " + std::to_string(encoding.id)
                        : result;
}

std::string
utf8ToAnsi(const std::string& value, AnsiEncoding encoding, bool* replaced) {
  if (replaced != nullptr) {
    *replaced = false;
  }
  const CFStringEncoding cfEncoding = toCFEncoding(encoding);
  if (cfEncoding == kCFStringEncodingUTF8 || value.empty()) {
    return value;
  }

  const CFHolder<CFStringRef> text =
      fromBytes(value.data(), value.size(), kCFStringEncodingUTF8);
  if (text.get() == nullptr) {
    throw std::runtime_error("Invalid UTF-8 returned by Trino");
  }
  return toBytes(text.get(), cfEncoding, replaced);
}

std::optional<std::string> ansiToUtf8(std::string_view bytes,
                                      AnsiEncoding encoding) {
  if (bytes.empty()) {
    return std::string();
  }
  const CFStringEncoding cfEncoding = toCFEncoding(encoding);
  const CFHolder<CFStringRef> text =
      fromBytes(bytes.data(), bytes.size(), cfEncoding);
  if (text.get() == nullptr) {
    return std::nullopt;
  }
  if (cfEncoding != kCFStringEncodingMacHebrew &&
      cfEncoding != kCFStringEncodingMacArabic &&
      cfEncoding != kCFStringEncodingMacFarsi) {
    return cfStringToUtf8(text.get());
  }
  // Core Foundation wraps runs of ASCII decoded from these in directional
  // marks that aren't in the bytes (and would break SQL text).
  const CFIndex length = CFStringGetLength(text.get());
  std::basic_string<UniChar> units;
  units.reserve(static_cast<std::size_t>(length));
  for (CFIndex i = 0; i < length; i++) {
    const UniChar unit = CFStringGetCharacterAtIndex(text.get(), i);
    if (!isBidiMark(unit)) {
      units.push_back(unit);
    }
  }
  const CFHolder<CFStringRef> stripped(CFStringCreateWithCharacters(
      kCFAllocatorDefault, units.data(), static_cast<CFIndex>(units.size())));
  return cfStringToUtf8(stripped.get());
}

SQLLEN calculateAnsiSafeCopyLength(const std::string& value,
                                   std::size_t byteOffset,
                                   SQLLEN maxBytes,
                                   AnsiEncoding encoding) {
  if (maxBytes <= 0 || byteOffset >= value.size()) {
    return 0;
  }

  const CFStringEncoding cfEncoding = toCFEncoding(encoding);
  if (cfEncoding == kCFStringEncodingUTF8) {
    return calculateUtf8SafeCopyLength(value, byteOffset, maxBytes);
  }

  const std::size_t available =
      std::min(value.size() - byteOffset, static_cast<std::size_t>(maxBytes));
  if (isSingleByte(cfEncoding) || available == value.size() - byteOffset) {
    return static_cast<SQLLEN>(available);
  }

  // Multi-byte legacy encodings (Mac Japanese, GB18030, ...): never end a
  // chunk inside a character. Back off until the chunk decodes.
  for (std::size_t back = 0; back < MAX_CHAR_BYTES && back < available;
       back++) {
    const std::size_t length = available - back;
    if (fromBytes(value.data() + byteOffset, length, cfEncoding).get() !=
        nullptr) {
      return static_cast<SQLLEN>(length);
    }
  }
  return 0;
}
