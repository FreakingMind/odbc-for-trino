#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "../../util/writeLog.hpp"
#include "unicodeWrappers.hpp"

/*
SQLGetDiagRecW: BufferLength and *TextLengthPtr count characters; the
SQLSTATE buffer holds 5 characters plus the terminator.

Diagnostic functions never post diagnostics of their own, so a truncated
message is only reported through SQL_SUCCESS_WITH_INFO.
*/

SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT HandleType,
                                 SQLHANDLE Handle,
                                 SQLSMALLINT RecNumber,
                                 SQLWCHAR* SqlStatePtr,
                                 SQLINTEGER* NativeErrorPtr,
                                 SQLWCHAR* MessageTextPtr,
                                 SQLSMALLINT BufferLength,
                                 SQLSMALLINT* TextLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetDiagRecW");
  if (!Handle) {
    return SQL_INVALID_HANDLE;
  }
  if (BufferLength < 0) {
    // Same as SQLGetDiagRec, which posts no diagnostics about itself.
    return SQL_ERROR;
  }
  return guardWideCall("SQLGetDiagRecW", [&]() {
    /*
    Long server errors are split into several records sized by the
    caller's BufferLength. Pass that size in characters as the byte size:
    a UTF-8 byte count is never smaller than the SQLWCHAR count of the same
    text, so every record still fits after conversion. Only when the ANSI
    call truncates (driver errors aren't split) is it called again with a
    buffer for the whole message. Without a message buffer the whole
    message is record 1, as in SQLGetDiagRec.
    */
    std::size_t initialBytes =
        MessageTextPtr
            ? static_cast<std::size_t>(std::max<SQLSMALLINT>(BufferLength, 1))
            : 1;
    // Widened narrow text can take up to 2 units per UTF-8 byte (GB18030:
    // 4 bytes for U+0080, 2 in UTF-8).
    const WideTextCodec codec = wideTextCodec(HandleType, Handle);
    if (codec.widenedAnsi && initialBytes > 1) {
      initialBytes = (initialBytes + 1) / 2;
    }

    // Room for the 5-character SQLSTATE plus slack for implementations that
    // don't bound it.
    std::array<SQLCHAR, 64> state{};
    std::string message;
    SQLRETURN rc = fetchNarrowStringWithBuffer<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          return SQLGetDiagRec(HandleType,
                               Handle,
                               RecNumber,
                               state.data(),
                               NativeErrorPtr,
                               buffer,
                               bytes,
                               length);
        },
        initialBytes,
        message);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }

    if (SqlStatePtr) {
      const auto* chars = reinterpret_cast<const char*>(state.data());
      const std::string sqlState(chars, strnlen(chars, 5));
      writeWideOutput<SQLSMALLINT>(sqlState,
                                   SqlStatePtr,
                                   6,
                                   nullptr,
                                   WideLengthUnit::Characters,
                                   wideTextCodec(HandleType, Handle));
    }
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(message,
                                     MessageTextPtr,
                                     BufferLength,
                                     TextLengthPtr,
                                     WideLengthUnit::Characters,
                                     wideTextCodec(HandleType, Handle));
    return reportTruncation(rc, truncated);
  });
}
