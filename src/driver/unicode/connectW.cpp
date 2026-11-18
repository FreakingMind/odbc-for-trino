#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "../../util/writeLog.hpp"
#include "../handles/connHandle.hpp"
#include "stringAttributes.hpp"
#include "unicodeWrappers.hpp"

/*
Unicode wrappers for the connection-level functions. String lengths:
  SQLConnectW, SQLDriverConnectW, SQLNativeSqlW: characters.
  SQLGetInfoW, SQLGet/SetConnectAttrW: bytes.
*/

SQLRETURN SQL_API SQLConnectW(SQLHDBC ConnectionHandle,
                              SQLWCHAR* ServerName,
                              SQLSMALLINT NameLength1,
                              SQLWCHAR* UserName,
                              SQLSMALLINT NameLength2,
                              SQLWCHAR* Authentication,
                              SQLSMALLINT NameLength3) {
  WriteLog(LL_TRACE, "Entering SQLConnectW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLConnectW", [&]() {
    NarrowInput dsn(ServerName, NameLength1, wideTextCodec(connection));
    NarrowInput user(UserName, NameLength2, wideTextCodec(connection));
    NarrowInput auth(Authentication, NameLength3, wideTextCodec(connection));
    return SQLConnect(ConnectionHandle,
                      dsn.data(),
                      dsn.smallLength(),
                      user.data(),
                      user.smallLength(),
                      auth.data(),
                      auth.smallLength());
  });
}

SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC ConnectionHandle,
                                    SQLHWND WindowHandle,
                                    SQLWCHAR* InConnectionString,
                                    SQLSMALLINT StringLength1,
                                    SQLWCHAR* OutConnectionString,
                                    SQLSMALLINT BufferLength,
                                    SQLSMALLINT* StringLength2Ptr,
                                    SQLUSMALLINT DriverCompletion) {
  WriteLog(LL_TRACE, "Entering SQLDriverConnectW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLDriverConnectW", [&]() {
    NarrowInput input(
        InConnectionString, StringLength1, wideTextCodec(connection));

    // SQLDriverConnect connects, so it can only be called once: give it an
    // output buffer that fits any completed connection string it can
    // report (its lengths are SQLSMALLINT).
    std::vector<SQLCHAR> output(static_cast<std::size_t>(SHRT_MAX) + 1, 0);
    SQLSMALLINT outputLength = 0;
    SQLRETURN rc             = SQLDriverConnect(ConnectionHandle,
                                                WindowHandle,
                                                input.data(),
                                                input.smallLength(),
                                                output.data(),
                                                SHRT_MAX,
                                                &outputLength,
                                                DriverCompletion);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const auto* chars = reinterpret_cast<const char*>(output.data());
    const std::string completed(chars, strnlen(chars, SHRT_MAX));
    bool truncated = false;
    if (InConnectionString != nullptr && completed == input.utf8()) {
      // The connection string comes back unchanged: return the
      // application's own text, whatever encoding it was read with.
      const std::size_t length =
          StringLength1 == SQL_NTS
              ? sqlWideLength(InConnectionString)
              : static_cast<std::size_t>(
                    std::max<SQLSMALLINT>(StringLength1, 0));
      SqlWideString original(length, SqlWideUnit{0});
      if (length > 0) {
        std::memcpy(
            original.data(), InConnectionString, length * sizeof(SQLWCHAR));
      }
      const WideCopyResult copy = copyToSqlWideBuffer(
          original,
          OutConnectionString,
          static_cast<std::size_t>(std::max<SQLSMALLINT>(BufferLength, 0)),
          wideTextCodec(connection));
      if (StringLength2Ptr) {
        *StringLength2Ptr = static_cast<SQLSMALLINT>(
            std::min<std::size_t>(copy.totalUnits, SHRT_MAX));
      }
      truncated = copy.truncated;
    } else {
      truncated = writeWideOutput<SQLSMALLINT>(completed,
                                               OutConnectionString,
                                               BufferLength,
                                               StringLength2Ptr,
                                               WideLengthUnit::Characters,
                                               wideTextCodec(connection));
    }
    return reportTruncation(connection, rc, truncated);
  });
}

SQLRETURN SQL_API SQLGetInfoW(SQLHDBC ConnectionHandle,
                              SQLUSMALLINT InfoType,
                              SQLPOINTER InfoValue,
                              SQLSMALLINT BufferLength,
                              SQLSMALLINT* StringLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetInfoW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringInfoType(InfoType)) {
    return SQLGetInfo(
        ConnectionHandle, InfoType, InfoValue, BufferLength, StringLengthPtr);
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLGetInfoW", [&]() {
    std::string value;
    // SQLGetInfo needs a buffer even to report the length.
    SQLRETURN rc = fetchNarrowStringWithBuffer<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          return SQLGetInfo(ConnectionHandle, InfoType, buffer, bytes, length);
        },
        SHRT_MAX,
        value);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(value,
                                     InfoValue,
                                     BufferLength,
                                     StringLengthPtr,
                                     WideLengthUnit::Bytes,
                                     wideTextCodec(connection));
    return reportTruncation(connection, rc, truncated);
  });
}

SQLRETURN SQL_API SQLGetConnectAttrW(SQLHDBC ConnectionHandle,
                                     SQLINTEGER Attribute,
                                     SQLPOINTER Value,
                                     SQLINTEGER BufferLength,
                                     SQLINTEGER* StringLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetConnectAttrW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringConnectAttribute(Attribute)) {
    return SQLGetConnectAttr(
        ConnectionHandle, Attribute, Value, BufferLength, StringLengthPtr);
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLGetConnectAttrW", [&]() {
    std::string value;
    SQLRETURN rc = fetchNarrowString<SQLINTEGER>(
        [&](SQLCHAR* buffer, SQLINTEGER bytes, SQLINTEGER* length) {
          return SQLGetConnectAttr(
              ConnectionHandle, Attribute, buffer, bytes, length);
        },
        0,
        value);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLINTEGER>(value,
                                    Value,
                                    BufferLength,
                                    StringLengthPtr,
                                    WideLengthUnit::Bytes,
                                    wideTextCodec(connection));
    return reportTruncation(connection, rc, truncated);
  });
}

SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC ConnectionHandle,
                                     SQLINTEGER Attribute,
                                     SQLPOINTER Value,
                                     SQLINTEGER StringLength) {
  WriteLog(LL_TRACE, "Entering SQLSetConnectAttrW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringConnectAttribute(Attribute) || !Value) {
    return SQLSetConnectAttr(ConnectionHandle, Attribute, Value, StringLength);
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLSetConnectAttrW", [&]() {
    // String attribute lengths are in bytes.
    const SQLLEN lengthChars =
        StringLength == SQL_NTS
            ? SQL_NTS
            : static_cast<SQLLEN>(std::max<SQLINTEGER>(StringLength, 0) /
                                  static_cast<SQLINTEGER>(sizeof(SQLWCHAR)));
    NarrowInput value(reinterpret_cast<SQLWCHAR*>(Value),
                      lengthChars,
                      wideTextCodec(connection));
    return SQLSetConnectAttr(
        ConnectionHandle, Attribute, value.data(), value.intLength());
  });
}

SQLRETURN SQL_API SQLSetConnectOptionW(SQLHDBC ConnectionHandle,
                                       SQLUSMALLINT Option,
                                       SQLULEN Value) {
  WriteLog(LL_TRACE, "Entering SQLSetConnectOptionW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringConnectAttribute(Option) || Value == 0) {
    return SQLSetConnectOption(ConnectionHandle, Option, Value);
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLSetConnectOptionW", [&]() {
    // String options are NUL-terminated.
    NarrowInput value(
        reinterpret_cast<SQLWCHAR*>(Value), SQL_NTS, wideTextCodec(connection));
    return SQLSetConnectOption(
        ConnectionHandle, Option, reinterpret_cast<SQLULEN>(value.data()));
  });
}

SQLRETURN SQL_API SQLNativeSqlW(SQLHDBC ConnectionHandle,
                                SQLWCHAR* InStatementText,
                                SQLINTEGER TextLength1,
                                SQLWCHAR* OutStatementText,
                                SQLINTEGER BufferLength,
                                SQLINTEGER* TextLength2Ptr) {
  WriteLog(LL_TRACE, "Entering SQLNativeSqlW");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  return guardWideCall(connection, "SQLNativeSqlW", [&]() {
    NarrowInput input(InStatementText, TextLength1, wideTextCodec(connection));
    std::string value;
    SQLRETURN rc = fetchNarrowString<SQLINTEGER>(
        [&](SQLCHAR* buffer, SQLINTEGER bytes, SQLINTEGER* length) {
          return SQLNativeSql(ConnectionHandle,
                              input.data(),
                              input.intLength(),
                              buffer,
                              bytes,
                              length);
        },
        0,
        value);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLINTEGER>(value,
                                    OutStatementText,
                                    BufferLength,
                                    TextLength2Ptr,
                                    WideLengthUnit::Characters,
                                    wideTextCodec(connection));
    return reportTruncation(connection, rc, truncated);
  });
}
