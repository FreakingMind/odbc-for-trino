#pragma once

#include "wideText.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

/*
Building blocks for the thin Unicode ("W") wrappers around the driver's
ANSI entry points. The ANSI functions work on UTF-8; the wrappers convert
SQLWCHAR input to UTF-8, call them and convert their string output back.
*/

class Connection;
class Statement;

// The text codec of a handle's connection (see WideTextCodec): Unicode for
// environments, descriptors and null handles; for a connection that isn't
// connected yet, the codec of "auto" in this application.
WideTextCodec wideTextCodec(const Connection* connection);
WideTextCodec wideTextCodec(const Statement* statement);
WideTextCodec wideTextCodec(SQLSMALLINT handleType, SQLHANDLE handle);

// How an ODBC function counts BufferLength and the returned string length.
enum class WideLengthUnit {
  Characters,
  Bytes
};

/*
UTF-8 copy of a SQLWCHAR input argument, passed to an ANSI function.
A null input stays null (length 0), so the ANSI function sees exactly what
it would see from a null argument.
*/
class NarrowInput {
  public:
    NarrowInput(const SQLWCHAR* text,
                SQLLEN lengthChars,
                const WideTextCodec& codec)
        : isNull(text == nullptr),
          value(sqlWideToUtf8(text, lengthChars, codec)) {}

    SQLCHAR* data() {
      return isNull ? nullptr : reinterpret_cast<SQLCHAR*>(value.data());
    }

    // Length for an ANSI SQLSMALLINT length argument. Names too long for
    // it are passed NUL-terminated.
    SQLSMALLINT smallLength() const {
      if (isNull) {
        return 0;
      }
      return value.size() <= static_cast<std::size_t>(SHRT_MAX)
                 ? static_cast<SQLSMALLINT>(value.size())
                 : static_cast<SQLSMALLINT>(SQL_NTS);
    }

    // Length for an ANSI SQLINTEGER length argument (keeps embedded NULs).
    SQLINTEGER intLength() const {
      if (isNull) {
        return 0;
      }
      return value.size() <= static_cast<std::size_t>(INT_MAX)
                 ? static_cast<SQLINTEGER>(value.size())
                 : static_cast<SQLINTEGER>(SQL_NTS);
    }

    const std::string& utf8() const {
      return value;
    }

  private:
    bool isNull;
    std::string value;
};

/*
Writes a UTF-8 string to a SQLWCHAR output buffer. bufferLength and
*lengthPtr count characters or bytes depending on unit. The full length is
always reported (clamped to the length type). Returns true when the buffer
was too small and the output was truncated.
*/
template <typename LenT>
bool writeWideOutput(std::string_view utf8,
                     SQLPOINTER buffer,
                     LenT bufferLength,
                     LenT* lengthPtr,
                     WideLengthUnit unit,
                     const WideTextCodec& codec) {
  const SqlWideString wide  = utf8ToSqlWide(utf8, codec);
  std::size_t capacityUnits = 0;
  if (bufferLength > 0) {
    capacityUnits = static_cast<std::size_t>(bufferLength);
    if (unit == WideLengthUnit::Bytes) {
      capacityUnits /= sizeof(SQLWCHAR);
    }
  }
  const WideCopyResult copy = copyToSqlWideBuffer(
      wide, reinterpret_cast<SQLWCHAR*>(buffer), capacityUnits, codec);
  if (lengthPtr) {
    std::size_t length = copy.totalUnits;
    if (unit == WideLengthUnit::Bytes) {
      length *= sizeof(SQLWCHAR);
    }
    *lengthPtr = static_cast<LenT>(std::min<std::size_t>(
        length, static_cast<std::size_t>(std::numeric_limits<LenT>::max())));
  }
  return copy.truncated;
}

/*
Gets a complete string from an ANSI function that reports the full length
when called without an output buffer:
  call(SQLCHAR* buffer, LenT bufferBytes, LenT* lengthBytes) -> SQLRETURN
The first call has a null buffer and only learns the length; the second
gets the string into a buffer of at least minBufferBytes that fits it.
Some ANSI implementations ignore bufferBytes, so the buffer must always be
large enough for the whole string.
*/
template <typename LenT, typename Call>
SQLRETURN
fetchNarrowString(Call&& call, LenT minBufferBytes, std::string& out) {
  LenT length  = 0;
  SQLRETURN rc = call(static_cast<SQLCHAR*>(nullptr),
                      std::max<LenT>(minBufferBytes, 0),
                      &length);
  if (!SQL_SUCCEEDED(rc)) {
    return rc;
  }

  constexpr auto maxLen =
      static_cast<std::size_t>(std::numeric_limits<LenT>::max());
  std::size_t capacity = (length > 0 ? static_cast<std::size_t>(length) : 0);
  capacity             = std::min(capacity + 1, maxLen);
  if (minBufferBytes > 0) {
    capacity = std::max(capacity, static_cast<std::size_t>(minBufferBytes));
  }
  // One spare byte for implementations that write length + 1 bytes.
  std::vector<SQLCHAR> buffer(capacity + 1, 0);
  length = 0;
  rc     = call(buffer.data(), static_cast<LenT>(capacity), &length);
  if (!SQL_SUCCEEDED(rc)) {
    return rc;
  }
  const auto* chars = reinterpret_cast<const char*>(buffer.data());
  out.assign(chars, strnlen(chars, capacity));
  return rc;
}

/*
Like fetchNarrowString, for ANSI functions that require an output buffer.
Starts with initialBytes and retries once with a larger buffer when the
reported length doesn't fit.
*/
template <typename LenT, typename Call>
SQLRETURN fetchNarrowStringWithBuffer(Call&& call,
                                      std::size_t initialBytes,
                                      std::string& out) {
  constexpr auto maxLen =
      static_cast<std::size_t>(std::numeric_limits<LenT>::max());
  std::size_t capacity = std::clamp<std::size_t>(initialBytes, 1, maxLen);
  for (int attempt = 0; attempt < 2; attempt++) {
    // One spare byte for implementations that write length + 1 bytes.
    std::vector<SQLCHAR> buffer(capacity + 1, 0);
    LenT length  = 0;
    SQLRETURN rc = call(buffer.data(), static_cast<LenT>(capacity), &length);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const std::size_t needed =
        (length > 0 ? static_cast<std::size_t>(length) : 0) + 1;
    if (needed <= capacity || capacity == maxLen || attempt == 1) {
      const auto* chars = reinterpret_cast<const char*>(buffer.data());
      out.assign(chars, strnlen(chars, capacity));
      return rc;
    }
    capacity = std::min(needed, maxLen);
  }
  return SQL_ERROR; // Not reached.
}

// Records 01004 (string data, right truncated) on the handle when the
// ANSI call itself succeeded without a warning of its own, and returns the
// adjusted return code.
SQLRETURN reportTruncation(Statement* statement, SQLRETURN rc, bool truncated);
SQLRETURN
reportTruncation(Connection* connection, SQLRETURN rc, bool truncated);
// For handles without diagnostic storage (descriptors) and for the
// diagnostic functions themselves, which never post diagnostics.
SQLRETURN reportTruncation(SQLRETURN rc, bool truncated);

void logWideCallException(const char* functionName, const char* what) noexcept;
void recordWideCallException(Statement* statement,
                             const char* functionName,
                             const char* what) noexcept;
void recordWideCallException(Connection* connection,
                             const char* functionName,
                             const char* what) noexcept;

// Exception boundaries for the extern "C" W functions: anything thrown is
// logged, recorded on the handle when possible, and turned into SQL_ERROR.
template <typename Handle, typename Fn>
SQLRETURN
guardWideCall(Handle* handle, const char* functionName, Fn&& fn) noexcept {
  try {
    return fn();
  } catch (const std::exception& e) {
    logWideCallException(functionName, e.what());
    if (handle) {
      recordWideCallException(handle, functionName, e.what());
    }
  } catch (...) {
    logWideCallException(functionName, "unknown exception");
    if (handle) {
      recordWideCallException(handle, functionName, "unknown exception");
    }
  }
  return SQL_ERROR;
}

template <typename Fn>
SQLRETURN guardWideCall(const char* functionName, Fn&& fn) noexcept {
  try {
    return fn();
  } catch (const std::exception& e) {
    logWideCallException(functionName, e.what());
  } catch (...) {
    logWideCallException(functionName, "unknown exception");
  }
  return SQL_ERROR;
}
