#include "rowToBuffer.hpp"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "dateAndTimeUtils.hpp"
#include "decimalHelper.hpp"
#include "jsonValueToText.hpp"
#include "writeLog.hpp"

ColumnToBufferStatus::ColumnToBufferStatus(bool isSuccess,
                                           bool isVariableLength,
                                           bool isTruncated) {
  this->isSuccess        = isSuccess;
  this->isVariableLength = isVariableLength;
  this->isTruncated      = isTruncated;
}

ColumnToBufferStatus ColumnToBufferStatus::failure(std::string sqlState,
                                                   std::string errorMessage) {
  ColumnToBufferStatus status(false, false);
  status.sqlState     = std::move(sqlState);
  status.errorMessage = std::move(errorMessage);
  return status;
}

SQLLEN calculateUtf8SafeCopyLength(const std::string& value,
                                   std::size_t byteOffset,
                                   SQLLEN maxBytes) {
  if (maxBytes <= 0 || byteOffset >= value.size()) {
    return 0;
  }

  const std::size_t remaining = value.size() - byteOffset;
  std::size_t copyLength =
      std::min<std::size_t>(remaining, static_cast<std::size_t>(maxBytes));

  if (copyLength == remaining) {
    return static_cast<SQLLEN>(copyLength);
  }

  std::size_t safeEnd = byteOffset + copyLength;
  while (safeEnd > byteOffset &&
         (static_cast<unsigned char>(value[safeEnd]) & 0xC0) == 0x80) {
    --safeEnd;
  }

  if (safeEnd == byteOffset) {
    return 0;
  }

  return static_cast<SQLLEN>(safeEnd - byteOffset);
}

SQLLEN calculateAnsiChunkLength(const std::string& value,
                                std::size_t byteOffset,
                                SQLLEN maxBytes,
                                AnsiEncoding encoding) {
  if (maxBytes <= 0 || byteOffset >= value.size()) {
    return 0;
  }
  const SQLLEN safeLength =
      calculateAnsiSafeCopyLength(value, byteOffset, maxBytes, encoding);
  if (safeLength > 0) {
    return safeLength;
  }
  // Not even one character fits: split it rather than return nothing
  // forever.
  return static_cast<SQLLEN>(std::min<std::size_t>(
      value.size() - byteOffset, static_cast<std::size_t>(maxBytes)));
}

SQLLEN calculateWideSafeCopyLengthBytes(const std::wstring& value,
                                        std::size_t byteOffset,
                                        SQLLEN maxBytes) {
  if (maxBytes <= 0 || value.empty()) {
    return 0;
  }

  const std::size_t unitSize = sizeof(SQLWCHAR);
  if (byteOffset % unitSize != 0) {
    return 0;
  }

  const std::size_t startUnit = byteOffset / unitSize;
  if (startUnit >= value.size()) {
    return 0;
  }

  const std::size_t maxUnits = static_cast<std::size_t>(maxBytes) / unitSize;
  if (maxUnits == 0) {
    return 0;
  }

  std::size_t copyUnits = std::min(value.size() - startUnit, maxUnits);
  // Do not split a UTF-16 surrogate pair. UTF-32 (4-byte SQLWCHAR, e.g.
  // iODBC on macOS) has no surrogates.
  if constexpr (sizeof(SQLWCHAR) == 2) {
    if (startUnit + copyUnits < value.size() && copyUnits > 0) {
      const wchar_t last = value[startUnit + copyUnits - 1];
      const wchar_t next = value[startUnit + copyUnits];
      if (last >= 0xD800 && last <= 0xDBFF && next >= 0xDC00 &&
          next <= 0xDFFF) {
        --copyUnits;
      }
    }
  }
  return static_cast<SQLLEN>(copyUnits * unitSize);
}

namespace {

constexpr const char* SQLSTATE_RESTRICTED_TYPE = "07006";
constexpr const char* SQLSTATE_OUT_OF_RANGE    = "22003";
constexpr const char* SQLSTATE_INVALID_CAST    = "22018";

const char* MSG_RESTRICTED_TYPE = "Restricted data type attribute violation";
const char* MSG_OUT_OF_RANGE    = "Numeric value out of range";
const char* MSG_INVALID_CAST = "Invalid character value for cast specification";

// Thrown by the converters below and turned into a ColumnToBufferStatus.
struct ConversionFailure {
    const char* sqlState;
    const char* message;
};

[[noreturn]] void failRestrictedType() {
  throw ConversionFailure{SQLSTATE_RESTRICTED_TYPE, MSG_RESTRICTED_TYPE};
}

[[noreturn]] void failOutOfRange() {
  throw ConversionFailure{SQLSTATE_OUT_OF_RANGE, MSG_OUT_OF_RANGE};
}

[[noreturn]] void failInvalidCast() {
  throw ConversionFailure{SQLSTATE_INVALID_CAST, MSG_INVALID_CAST};
}

std::string_view trimSpaces(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

// Locale-independent strtod: the host application (Excel) may have set a
// locale with a decimal comma. Also understands the strings Trino uses for
// the special DOUBLE/REAL values.
double parseDoubleText(std::string_view text) {
  text = trimSpaces(text);
  if (text == "NaN") {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (text == "Infinity" || text == "+Infinity") {
    return std::numeric_limits<double>::infinity();
  }
  if (text == "-Infinity") {
    return -std::numeric_limits<double>::infinity();
  }
  if (text.empty()) {
    failInvalidCast();
  }
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  double value = 0;
  stream >> value;
  if (stream.fail() || stream.peek() != std::char_traits<char>::eof()) {
    failInvalidCast();
  }
  return value;
}

template <typename T> T checkedIntegralFromDouble(double value) {
  if (std::isnan(value)) {
    failInvalidCast();
  }
  // Conversions to integers truncate the fractional part.
  const double truncated = std::trunc(value);
  if (truncated < static_cast<double>(std::numeric_limits<T>::min()) ||
      truncated >= std::ldexp(1.0, std::numeric_limits<T>::digits)) {
    failOutOfRange();
  }
  return static_cast<T>(truncated);
}

template <typename T, typename Source> T checkedIntegral(Source value) {
  if (!std::in_range<T>(value)) {
    failOutOfRange();
  }
  return static_cast<T>(value);
}

// Integer text, optionally with a fractional part that is truncated
// ("123.45" from a DECIMAL column), parsed exactly without going through
// double. Anything else (exponents, ...) is parsed as a double.
template <typename T> T parseIntegralText(std::string_view text) {
  text = trimSpaces(text);
  if (!text.empty() && text.front() == '+') {
    text.remove_prefix(1);
  }
  std::string_view whole = text;
  std::string_view fraction;
  if (const auto dot = text.find('.'); dot != std::string_view::npos) {
    whole    = text.substr(0, dot);
    fraction = text.substr(dot + 1);
  }
  const bool fractionIsDigits =
      std::all_of(fraction.begin(), fraction.end(), [](char c) {
        return c >= '0' && c <= '9';
      });
  if (!whole.empty() && whole != "-" && fractionIsDigits) {
    if (whole.front() == '-') {
      int64_t value = 0;
      const auto [end, ec] =
          std::from_chars(whole.data(), whole.data() + whole.size(), value);
      if (ec == std::errc::result_out_of_range) {
        failOutOfRange();
      }
      if (ec == std::errc() && end == whole.data() + whole.size()) {
        // "-0.5" truncates to 0, which is fine for unsigned types too.
        return value == 0 ? T(0) : checkedIntegral<T>(value);
      }
    } else {
      uint64_t value = 0;
      const auto [end, ec] =
          std::from_chars(whole.data(), whole.data() + whole.size(), value);
      if (ec == std::errc::result_out_of_range) {
        failOutOfRange();
      }
      if (ec == std::errc() && end == whole.data() + whole.size()) {
        return checkedIntegral<T>(value);
      }
    }
  }
  return checkedIntegralFromDouble<T>(parseDoubleText(text));
}

template <typename T> T jsonToIntegral(const json& value) {
  switch (value.type()) {
    case json::value_t::number_integer:
      return checkedIntegral<T>(value.get<int64_t>());
    case json::value_t::number_unsigned:
      return checkedIntegral<T>(value.get<uint64_t>());
    case json::value_t::number_float:
      return checkedIntegralFromDouble<T>(value.get<double>());
    case json::value_t::boolean:
      return static_cast<T>(value.get<bool>() ? 1 : 0);
    case json::value_t::string:
      return parseIntegralText<T>(value.get_ref<const std::string&>());
    default:
      failRestrictedType();
  }
}

double jsonToDouble(const json& value) {
  switch (value.type()) {
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
    case json::value_t::number_float:
      return value.get<double>();
    case json::value_t::boolean:
      return value.get<bool>() ? 1.0 : 0.0;
    case json::value_t::string:
      // REAL/DOUBLE NaN and infinities arrive as JSON strings.
      return parseDoubleText(value.get_ref<const std::string&>());
    default:
      failRestrictedType();
  }
}

float jsonToFloat(const json& value) {
  const double d = jsonToDouble(value);
  if (std::isfinite(d) && std::fabs(d) > static_cast<double>(FLT_MAX)) {
    failOutOfRange();
  }
  return static_cast<float>(d);
}

unsigned char jsonToBit(const json& value) {
  if (value.is_boolean()) {
    return value.get<bool>() ? 1 : 0;
  }
  // Numbers: 0 and 1 convert exactly; anything in (0, 2) is truncated to 1
  // (ODBC's 01S07 case); anything else is out of range.
  const double d = jsonToDouble(value);
  if (std::isnan(d)) {
    failInvalidCast();
  }
  if (d < 0 || d >= 2) {
    failOutOfRange();
  }
  return d > 0 ? 1 : 0;
}

template <typename T>
void writeFixed(T value, void* buffer, SQLLEN* strLen_or_IndPtr) {
  std::memcpy(buffer, &value, sizeof(T));
  if (strLen_or_IndPtr) {
    *strLen_or_IndPtr = sizeof(T);
  }
}

// Trino's text forms of date/time values.
bool looksLikeDate(const std::string& value) { // YYYY-MM-DD...
  return value.size() >= 10 && value[4] == '-' && value[7] == '-';
}

bool looksLikeTimestamp(const std::string& value) { // YYYY-MM-DD HH:MM:SS...
  return value.size() >= 19 && looksLikeDate(value) && value[10] == ' ' &&
         value[13] == ':' && value[16] == ':';
}

bool looksLikeTime(const std::string& value) { // HH:MM:SS...
  return value.size() >= 8 && value[2] == ':' && value[5] == ':';
}

// Today's date in the local time zone.
SQL_DATE_STRUCT localToday() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  SQL_DATE_STRUCT today{};
  today.year  = static_cast<SQLSMALLINT>(local.tm_year + 1900);
  today.month = static_cast<SQLUSMALLINT>(local.tm_mon + 1);
  today.day   = static_cast<SQLUSMALLINT>(local.tm_mday);
  return today;
}

const std::string& jsonString(const json& value) {
  if (!value.is_string()) {
    failRestrictedType();
  }
  return value.get_ref<const std::string&>();
}

// Character data: every Trino value has a text form (jsonValueToText).
ColumnToBufferStatus copyStrToBuffer(const json& jsonValue,
                                     SQLSMALLINT cDataType,
                                     void* buffer,
                                     SQLLEN bufferLength,
                                     SQLLEN* strLen_or_IndPtr,
                                     AnsiEncoding ansiEncoding) {
  const std::string utf8Value = jsonValueToText(jsonValue);

  if (cDataType == SQL_C_WCHAR) {
    const std::wstring value = utf8ToWide(utf8Value);
    const SQLLEN valueBytes =
        static_cast<SQLLEN>(value.size() * sizeof(SQLWCHAR));
    if (strLen_or_IndPtr) {
      *strLen_or_IndPtr = valueBytes;
    }

    const SQLLEN terminatorBytes = sizeof(SQLWCHAR);
    const SQLLEN payloadCapacity =
        bufferLength >= terminatorBytes ? bufferLength - terminatorBytes : 0;
    const SQLLEN copyBytes =
        calculateWideSafeCopyLengthBytes(value, 0, payloadCapacity);

    if (buffer && copyBytes > 0) {
      std::memcpy(buffer, value.data(), static_cast<std::size_t>(copyBytes));
    }
    if (buffer && bufferLength >= terminatorBytes) {
      const std::size_t terminatorIndex =
          static_cast<std::size_t>(copyBytes) / sizeof(SQLWCHAR);
      static_cast<SQLWCHAR*>(buffer)[terminatorIndex] = 0;
    }
    return ColumnToBufferStatus(true, true, copyBytes < valueBytes);
  }

  bool replaced           = false;
  const std::string value = utf8ToAnsi(utf8Value, ansiEncoding, &replaced);
  if (replaced) {
    WriteLog(LL_WARN,
             "  WARNING: Characters unavailable in the narrow encoding (" +
                 ansiEncodingName(ansiEncoding) + ") were replaced with ?");
  }
  if (strLen_or_IndPtr) {
    *strLen_or_IndPtr = static_cast<SQLLEN>(value.size());
  }

  const SQLLEN payloadCapacity = bufferLength > 0 ? bufferLength - 1 : 0;
  const SQLLEN copyLength =
      calculateAnsiChunkLength(value, 0, payloadCapacity, ansiEncoding);

  if (buffer && copyLength > 0) {
    std::memcpy(buffer, value.data(), static_cast<std::size_t>(copyLength));
  }
  if (buffer && bufferLength > 0) {
    static_cast<char*>(buffer)[copyLength] = '\0';
  }
  return ColumnToBufferStatus(
      true, true, static_cast<std::size_t>(copyLength) < value.size());
}

void copyDecimalToBuffer(const std::string& valueString,
                         void* buffer,
                         SQLLEN* strLen_or_IndPtr,
                         SQLCHAR precision) {
  std::string_view text = trimSpaces(valueString);
  bool isPositive       = true;
  if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
    isPositive = text.front() == '+';
    text.remove_prefix(1);
  }

  // Use the position of the decimal place if it exists
  // to parse the whole and fractional component of the decimal.
  std::string_view wholePart = text;
  std::string_view fractionalPart;
  if (const size_t dotPosition = text.find('.');
      dotPosition != std::string_view::npos) {
    wholePart      = text.substr(0, dotPosition);
    fractionalPart = text.substr(dotPosition + 1);
  }
  std::string digits = std::string(wholePart) + std::string(fractionalPart);
  if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) {
        return c >= '0' && c <= '9';
      })) {
    failInvalidCast();
  }

  std::string lsbEncodedValue;
  try {
    lsbEncodedValue = lsbDecimalEncoder(digits);
  } catch (const std::out_of_range&) {
    failOutOfRange();
  }
  if (lsbEncodedValue.size() > SQL_MAX_NUMERIC_LEN) {
    failOutOfRange();
  }

  SQL_NUMERIC_STRUCT numeric{};
  // The scale is the number of fractional digits that were encoded; the
  // precision cannot be inferred from the text ("1" may be DECIMAL(38)), so
  // it comes from the column metadata.
  numeric.scale     = static_cast<SQLSCHAR>(fractionalPart.size());
  numeric.precision = std::max<SQLCHAR>(
      precision, static_cast<SQLCHAR>(std::min<size_t>(digits.size(), 38)));
  numeric.sign = isPositive ? 1 : 0;
  std::memcpy(numeric.val, lsbEncodedValue.data(), lsbEncodedValue.size());
  writeFixed(numeric, buffer, strLen_or_IndPtr);
}

void copyGuidToBuffer(const std::string& value,
                      void* buffer,
                      SQLLEN* strLen_or_IndPtr) {
  // Trino UUIDs are strings, "00000000-0000-0000-0000-000000000000".
  if (value.size() != 36 || value[8] != '-' || value[13] != '-' ||
      value[18] != '-' || value[23] != '-') {
    failInvalidCast();
  }
  auto parseHex = [&value](size_t offset, size_t length, uint64_t& out) {
    const char* begin    = value.data() + offset;
    const auto [end, ec] = std::from_chars(begin, begin + length, out, 16);
    if (ec != std::errc() || end != begin + length) {
      failInvalidCast();
    }
  };
  uint64_t data1 = 0, data2 = 0, data3 = 0, data4Part1 = 0, data4Part2 = 0;
  parseHex(0, 8, data1);
  parseHex(9, 4, data2);
  parseHex(14, 4, data3);
  parseHex(19, 4, data4Part1);
  parseHex(24, 12, data4Part2);

  SQLGUID guid{};
  guid.Data1 = static_cast<decltype(guid.Data1)>(data1);
  guid.Data2 = static_cast<decltype(guid.Data2)>(data2);
  guid.Data3 = static_cast<decltype(guid.Data3)>(data3);
  // Data4 holds the last 8 bytes in the order they appear in the text.
  guid.Data4[0] = static_cast<unsigned char>((data4Part1 >> 8) & 0xFF);
  guid.Data4[1] = static_cast<unsigned char>(data4Part1 & 0xFF);
  for (int i = 0; i < 6; i++) {
    guid.Data4[2 + i] =
        static_cast<unsigned char>((data4Part2 >> (40 - 8 * i)) & 0xFF);
  }
  writeFixed(guid, buffer, strLen_or_IndPtr);
}

} // namespace

ColumnToBufferStatus columnToBuffer(SQLSMALLINT cDataType,
                                    SQLSMALLINT odbcDataType,
                                    const json& rowData,
                                    SQLULEN columnNumber,
                                    void* buffer,
                                    SQLLEN bufferLength,
                                    SQLLEN* strLen_or_IndPtr,
                                    SQLCHAR precision,
                                    SQLCHAR scale,
                                    AnsiEncoding ansiEncoding) {
  static_cast<void>(odbcDataType);
  static_cast<void>(scale);
  if (!rowData.is_array() || columnNumber < 1 ||
      columnNumber > rowData.size()) {
    return ColumnToBufferStatus::failure("07009", "Invalid descriptor index");
  }
  const json& jsonValue = rowData[columnNumber - 1];

  if (jsonValue.is_null()) {
    if (!strLen_or_IndPtr) {
      return ColumnToBufferStatus::failure(
          "22002", "Indicator variable required but not supplied");
    }
    *strLen_or_IndPtr = SQL_NULL_DATA;
    if (buffer && bufferLength > 0 && cDataType == SQL_C_CHAR) {
      static_cast<char*>(buffer)[0] = '\0';
    }
    if (buffer && bufferLength >= static_cast<SQLLEN>(sizeof(SQLWCHAR)) &&
        cDataType == SQL_C_WCHAR) {
      static_cast<SQLWCHAR*>(buffer)[0] = 0;
    }
    return ColumnToBufferStatus(true, true, false);
  }

  if (cDataType == SQL_C_CHAR || cDataType == SQL_C_WCHAR) {
    try {
      return copyStrToBuffer(jsonValue,
                             cDataType,
                             buffer,
                             bufferLength,
                             strLen_or_IndPtr,
                             ansiEncoding);
    } catch (const std::exception& e) {
      WriteLog(LL_ERROR,
               "  ERROR: converting column " + std::to_string(columnNumber) +
                   " to character data: " + e.what());
      return ColumnToBufferStatus::failure(SQLSTATE_RESTRICTED_TYPE,
                                           MSG_RESTRICTED_TYPE);
    }
  }

  // Everything below writes a fixed-length value.
  if (buffer == nullptr) {
    return ColumnToBufferStatus::failure("HY009",
                                         "Invalid use of null pointer");
  }

  try {
    switch (cDataType) {
      case SQL_C_NUMERIC: { // 2
        // Trino decimals return as strings, '123.456'
        copyDecimalToBuffer(
            jsonValueToText(jsonValue), buffer, strLen_or_IndPtr, precision);
        break;
      }
      case SQL_C_GUID: { // -11
        copyGuidToBuffer(jsonString(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_DATE:        // 9
      case SQL_C_TYPE_DATE: { // 91
        // "YYYY-MM-DD", or the date of a timestamp as Trino prints it (its
        // own wall-clock date, like CAST(... AS date) in Trino). ODBC asks
        // for 01S07 when the time part is dropped; Excel doesn't need it.
        const std::string& value = jsonString(jsonValue);
        if (!looksLikeDate(value)) {
          failInvalidCast();
        }
        writeFixed(parseDate(value), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_TIME:        // 10
      case SQL_C_TYPE_TIME: { // 92
        // "HH:MM:SS[.fff]", or the time of a timestamp as Trino prints it
        // (its wall-clock time).
        const std::string& value = jsonString(jsonValue);
        if (looksLikeTimestamp(value)) {
          writeFixed(parseTime(value.substr(11)), buffer, strLen_or_IndPtr);
        } else if (looksLikeTime(value)) {
          writeFixed(parseTime(value), buffer, strLen_or_IndPtr);
        } else {
          failInvalidCast();
        }
        break;
      }
      case SQL_C_TIMESTAMP:        // 11
      case SQL_C_TYPE_TIMESTAMP: { // 93
        // "YYYY-MM-DD HH:MM:SS[.fff][ zone]", converted to UTC when it has a
        // zone. ODBC also converts dates (midnight) and times (today's local
        // date, no fraction): Excel for Mac binds DATE columns as timestamps.
        const std::string& value = jsonString(jsonValue);
        ParsedTimestamp ts{};
        if (looksLikeTimestamp(value)) {
          ts = parseTimestamp(value);
        } else if (looksLikeDate(value)) {
          ts.date = parseDate(value);
        } else if (looksLikeTime(value)) {
          ts.date = localToday();
          ts.time = parseTime(value);
        } else {
          failInvalidCast();
        }
        SQL_TIMESTAMP_STRUCT timestamp{};
        timestamp.year     = ts.date.year;
        timestamp.month    = ts.date.month;
        timestamp.day      = ts.date.day;
        timestamp.hour     = ts.time.hour;
        timestamp.minute   = ts.time.minute;
        timestamp.second   = ts.time.second;
        timestamp.fraction = ts.fraction;
        writeFixed(timestamp, buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_BIT: { // -7
        writeFixed(jsonToBit(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_TINYINT:    // -6
      case SQL_C_STINYINT: { // -26
        writeFixed(jsonToIntegral<int8_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_UTINYINT: { // -28
        writeFixed(
            jsonToIntegral<uint8_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_SHORT:    // 5
      case SQL_C_SSHORT: { // -15
        writeFixed(
            jsonToIntegral<int16_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_USHORT: { // -17
        writeFixed(
            jsonToIntegral<uint16_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_LONG:    // 4
      case SQL_C_SLONG: { // -16
        writeFixed(
            jsonToIntegral<int32_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_ULONG: { // -18
        writeFixed(
            jsonToIntegral<uint32_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_BIGINT:      // -5
      case SQL_C_SBIGINT: { // -25
        writeFixed(
            jsonToIntegral<int64_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_UBIGINT: { // -27
        writeFixed(
            jsonToIntegral<uint64_t>(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_FLOAT: { // 7
        writeFixed(jsonToFloat(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      case SQL_C_DOUBLE: { // 8
        writeFixed(jsonToDouble(jsonValue), buffer, strLen_or_IndPtr);
        break;
      }
      default: {
        WriteLog(LL_ERROR,
                 "  ERROR: Cannot convert column " +
                     std::to_string(columnNumber) + " to C type " +
                     std::to_string(cDataType));
        return ColumnToBufferStatus::failure(SQLSTATE_RESTRICTED_TYPE,
                                             MSG_RESTRICTED_TYPE);
      }
    }
  } catch (const ConversionFailure& failure) {
    WriteLog(LL_ERROR,
             "  ERROR: converting column " + std::to_string(columnNumber) +
                 " to C type " + std::to_string(cDataType) + ": " +
                 failure.message);
    return ColumnToBufferStatus::failure(failure.sqlState, failure.message);
  } catch (const std::exception& e) {
    WriteLog(LL_ERROR,
             "  ERROR: converting column " + std::to_string(columnNumber) +
                 " to C type " + std::to_string(cDataType) + ": " + e.what());
    return jsonValue.is_string()
               ? ColumnToBufferStatus::failure(SQLSTATE_INVALID_CAST,
                                               MSG_INVALID_CAST)
               : ColumnToBufferStatus::failure(SQLSTATE_RESTRICTED_TYPE,
                                               MSG_RESTRICTED_TYPE);
  }
  return ColumnToBufferStatus(true, false);
}
