#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <algorithm>
#include <string>

#include "../../util/writeLog.hpp"
#include "stringAttributes.hpp"
#include "unicodeWrappers.hpp"

/*
Unicode wrappers for the descriptor functions. String lengths:
  SQLGet/SetDescFieldW: bytes.
  SQLGetDescRecW: characters.
Descriptors keep no diagnostic records in this driver, so truncation is only
reported through SQL_SUCCESS_WITH_INFO.
*/

SQLRETURN SQL_API SQLGetDescFieldW(SQLHDESC DescriptorHandle,
                                   SQLSMALLINT RecNumber,
                                   SQLSMALLINT FieldIdentifier,
                                   SQLPOINTER Value,
                                   SQLINTEGER BufferLength,
                                   SQLINTEGER* StringLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetDescFieldW");
  if (!DescriptorHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringDescriptorField(FieldIdentifier)) {
    return SQLGetDescField(DescriptorHandle,
                           RecNumber,
                           FieldIdentifier,
                           Value,
                           BufferLength,
                           StringLengthPtr);
  }
  return guardWideCall("SQLGetDescFieldW", [&]() {
    std::string value;
    // SQLGetDescField needs a buffer even to report the length.
    SQLRETURN rc = fetchNarrowStringWithBuffer<SQLINTEGER>(
        [&](SQLCHAR* buffer, SQLINTEGER bytes, SQLINTEGER* length) {
          return SQLGetDescField(DescriptorHandle,
                                 RecNumber,
                                 FieldIdentifier,
                                 buffer,
                                 bytes,
                                 length);
        },
        4096,
        value);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated = writeWideOutput<SQLINTEGER>(value,
                                                       Value,
                                                       BufferLength,
                                                       StringLengthPtr,
                                                       WideLengthUnit::Bytes,
                                                       WideTextCodec{});
    return reportTruncation(rc, truncated);
  });
}

SQLRETURN SQL_API SQLSetDescFieldW(SQLHDESC DescriptorHandle,
                                   SQLSMALLINT RecNumber,
                                   SQLSMALLINT FieldIdentifier,
                                   SQLPOINTER Value,
                                   SQLINTEGER BufferLength) {
  WriteLog(LL_TRACE, "Entering SQLSetDescFieldW");
  if (!DescriptorHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringDescriptorField(FieldIdentifier) || !Value) {
    return SQLSetDescField(
        DescriptorHandle, RecNumber, FieldIdentifier, Value, BufferLength);
  }
  return guardWideCall("SQLSetDescFieldW", [&]() {
    // String field lengths are in bytes.
    const SQLLEN lengthChars =
        BufferLength == SQL_NTS
            ? SQL_NTS
            : static_cast<SQLLEN>(std::max<SQLINTEGER>(BufferLength, 0) /
                                  static_cast<SQLINTEGER>(sizeof(SQLWCHAR)));
    NarrowInput value(
        reinterpret_cast<SQLWCHAR*>(Value), lengthChars, WideTextCodec{});
    return SQLSetDescField(DescriptorHandle,
                           RecNumber,
                           FieldIdentifier,
                           value.data(),
                           value.intLength());
  });
}

SQLRETURN SQL_API SQLGetDescRecW(SQLHDESC DescriptorHandle,
                                 SQLSMALLINT RecNumber,
                                 SQLWCHAR* Name,
                                 SQLSMALLINT BufferLength,
                                 SQLSMALLINT* StringLengthPtr,
                                 SQLSMALLINT* TypePtr,
                                 SQLSMALLINT* SubTypePtr,
                                 SQLLEN* LengthPtr,
                                 SQLSMALLINT* PrecisionPtr,
                                 SQLSMALLINT* ScalePtr,
                                 SQLSMALLINT* NullablePtr) {
  WriteLog(LL_TRACE, "Entering SQLGetDescRecW");
  if (!DescriptorHandle) {
    return SQL_INVALID_HANDLE;
  }
  return guardWideCall("SQLGetDescRecW", [&]() {
    std::string name;
    bool fetchingName = false;
    SQLRETURN rc      = fetchNarrowString<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          // The other output arguments are written by the second call.
          const bool lastCall = fetchingName;
          fetchingName        = true;
          return SQLGetDescRec(DescriptorHandle,
                               RecNumber,
                               buffer,
                               bytes,
                               length,
                               lastCall ? TypePtr : nullptr,
                               lastCall ? SubTypePtr : nullptr,
                               lastCall ? LengthPtr : nullptr,
                               lastCall ? PrecisionPtr : nullptr,
                               lastCall ? ScalePtr : nullptr,
                               lastCall ? NullablePtr : nullptr);
        },
        0,
        name);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(name,
                                     Name,
                                     BufferLength,
                                     StringLengthPtr,
                                     WideLengthUnit::Characters,
                                     WideTextCodec{});
    return reportTruncation(rc, truncated);
  });
}
