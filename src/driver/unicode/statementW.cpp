#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>
#include <sqlucode.h>

#include <string>

#include "../../util/writeLog.hpp"
#include "../handles/statementHandle.hpp"
#include "stringAttributes.hpp"
#include "unicodeWrappers.hpp"

/*
Unicode wrappers for the statement-level functions. String lengths:
  SQLExecDirectW, SQLPrepareW, SQLGet/SetCursorNameW, SQLDescribeColW and
  the catalog functions: characters.
  SQLColAttributeW: bytes.
Statement attributes have no string values, so SQLGet/SetStmtAttrW pass
through.
*/

SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT StatementHandle,
                                 SQLWCHAR* StatementText,
                                 SQLINTEGER TextLength) {
  WriteLog(LL_TRACE, "Entering SQLExecDirectW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLExecDirectW", [&]() {
    NarrowInput text(StatementText, TextLength, wideTextCodec(statement));
    return SQLExecDirect(StatementHandle, text.data(), text.intLength());
  });
}

SQLRETURN SQL_API SQLPrepareW(SQLHSTMT StatementHandle,
                              SQLWCHAR* StatementText,
                              SQLINTEGER TextLength) {
  WriteLog(LL_TRACE, "Entering SQLPrepareW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLPrepareW", [&]() {
    NarrowInput text(StatementText, TextLength, wideTextCodec(statement));
    return SQLPrepare(StatementHandle, text.data(), text.intLength());
  });
}

SQLRETURN SQL_API SQLGetCursorNameW(SQLHSTMT StatementHandle,
                                    SQLWCHAR* CursorName,
                                    SQLSMALLINT BufferLength,
                                    SQLSMALLINT* NameLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetCursorNameW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLGetCursorNameW", [&]() {
    std::string name;
    SQLRETURN rc = fetchNarrowString<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          return SQLGetCursorName(StatementHandle, buffer, bytes, length);
        },
        0,
        name);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(name,
                                     CursorName,
                                     BufferLength,
                                     NameLengthPtr,
                                     WideLengthUnit::Characters,
                                     wideTextCodec(statement));
    return reportTruncation(statement, rc, truncated);
  });
}

SQLRETURN SQL_API SQLSetCursorNameW(SQLHSTMT StatementHandle,
                                    SQLWCHAR* CursorName,
                                    SQLSMALLINT NameLength) {
  WriteLog(LL_TRACE, "Entering SQLSetCursorNameW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLSetCursorNameW", [&]() {
    NarrowInput name(CursorName, NameLength, wideTextCodec(statement));
    return SQLSetCursorName(StatementHandle, name.data(), name.smallLength());
  });
}

SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT StatementHandle,
                                  SQLINTEGER Attribute,
                                  SQLPOINTER Value,
                                  SQLINTEGER BufferLength,
                                  SQLINTEGER* StringLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetStmtAttrW");
  return SQLGetStmtAttr(
      StatementHandle, Attribute, Value, BufferLength, StringLengthPtr);
}

SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT StatementHandle,
                                  SQLINTEGER Attribute,
                                  SQLPOINTER Value,
                                  SQLINTEGER StringLength) {
  WriteLog(LL_TRACE, "Entering SQLSetStmtAttrW");
  return SQLSetStmtAttr(StatementHandle, Attribute, Value, StringLength);
}

SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT StatementHandle,
                                  SQLUSMALLINT ColumnNumber,
                                  SQLWCHAR* ColumnName,
                                  SQLSMALLINT BufferLength,
                                  SQLSMALLINT* NameLengthPtr,
                                  SQLSMALLINT* DataTypePtr,
                                  SQLULEN* ColumnSizePtr,
                                  SQLSMALLINT* DecimalDigitsPtr,
                                  SQLSMALLINT* NullablePtr) {
  WriteLog(LL_TRACE, "Entering SQLDescribeColW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLDescribeColW", [&]() {
    std::string name;
    bool fetchingName = false;
    SQLRETURN rc      = fetchNarrowString<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          // The first call only learns the name length; the other output
          // arguments are written by the second one.
          const bool lastCall = fetchingName;
          fetchingName        = true;
          return SQLDescribeCol(StatementHandle,
                                ColumnNumber,
                                buffer,
                                bytes,
                                length,
                                lastCall ? DataTypePtr : nullptr,
                                lastCall ? ColumnSizePtr : nullptr,
                                lastCall ? DecimalDigitsPtr : nullptr,
                                lastCall ? NullablePtr : nullptr);
        },
        0,
        name);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(name,
                                     ColumnName,
                                     BufferLength,
                                     NameLengthPtr,
                                     WideLengthUnit::Characters,
                                     wideTextCodec(statement));
    return reportTruncation(statement, rc, truncated);
  });
}

// The type of SQLColAttribute's NumericAttributePtr differs between the
// Windows 32-bit headers and everything else (see colAttribute.cpp).
#if defined(_WIN64) || !defined(_WIN32)
using ColAttributeNumericPtr = SQLLEN*;
#else
using ColAttributeNumericPtr = SQLPOINTER;
#endif

SQLRETURN SQL_API SQLColAttributeW(SQLHSTMT StatementHandle,
                                   SQLUSMALLINT ColumnNumber,
                                   SQLUSMALLINT FieldIdentifier,
                                   SQLPOINTER CharacterAttributePtr,
                                   SQLSMALLINT BufferLength,
                                   SQLSMALLINT* StringLengthPtr,
                                   ColAttributeNumericPtr NumericAttributePtr) {
  WriteLog(LL_TRACE, "Entering SQLColAttributeW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  if (!isStringDescriptorField(static_cast<SQLSMALLINT>(FieldIdentifier))) {
    return SQLColAttribute(StatementHandle,
                           ColumnNumber,
                           FieldIdentifier,
                           CharacterAttributePtr,
                           BufferLength,
                           StringLengthPtr,
                           NumericAttributePtr);
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLColAttributeW", [&]() {
    std::string value;
    SQLRETURN rc = fetchNarrowString<SQLSMALLINT>(
        [&](SQLCHAR* buffer, SQLSMALLINT bytes, SQLSMALLINT* length) {
          return SQLColAttribute(StatementHandle,
                                 ColumnNumber,
                                 FieldIdentifier,
                                 buffer,
                                 bytes,
                                 length,
                                 nullptr);
        },
        0,
        value);
    if (!SQL_SUCCEEDED(rc)) {
      return rc;
    }
    // CharacterAttributePtr lengths are in bytes.
    const bool truncated =
        writeWideOutput<SQLSMALLINT>(value,
                                     CharacterAttributePtr,
                                     BufferLength,
                                     StringLengthPtr,
                                     WideLengthUnit::Bytes,
                                     wideTextCodec(statement));
    return reportTruncation(statement, rc, truncated);
  });
}

SQLRETURN SQL_API SQLTablesW(SQLHSTMT StatementHandle,
                             SQLWCHAR* CatalogName,
                             SQLSMALLINT NameLength1,
                             SQLWCHAR* SchemaName,
                             SQLSMALLINT NameLength2,
                             SQLWCHAR* TableName,
                             SQLSMALLINT NameLength3,
                             SQLWCHAR* TableType,
                             SQLSMALLINT NameLength4) {
  WriteLog(LL_TRACE, "Entering SQLTablesW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLTablesW", [&]() {
    NarrowInput catalog(CatalogName, NameLength1, wideTextCodec(statement));
    NarrowInput schema(SchemaName, NameLength2, wideTextCodec(statement));
    NarrowInput table(TableName, NameLength3, wideTextCodec(statement));
    NarrowInput type(TableType, NameLength4, wideTextCodec(statement));
    return SQLTables(StatementHandle,
                     catalog.data(),
                     catalog.smallLength(),
                     schema.data(),
                     schema.smallLength(),
                     table.data(),
                     table.smallLength(),
                     type.data(),
                     type.smallLength());
  });
}

SQLRETURN SQL_API SQLColumnsW(SQLHSTMT StatementHandle,
                              SQLWCHAR* CatalogName,
                              SQLSMALLINT NameLength1,
                              SQLWCHAR* SchemaName,
                              SQLSMALLINT NameLength2,
                              SQLWCHAR* TableName,
                              SQLSMALLINT NameLength3,
                              SQLWCHAR* ColumnName,
                              SQLSMALLINT NameLength4) {
  WriteLog(LL_TRACE, "Entering SQLColumnsW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLColumnsW", [&]() {
    NarrowInput catalog(CatalogName, NameLength1, wideTextCodec(statement));
    NarrowInput schema(SchemaName, NameLength2, wideTextCodec(statement));
    NarrowInput table(TableName, NameLength3, wideTextCodec(statement));
    NarrowInput column(ColumnName, NameLength4, wideTextCodec(statement));
    return SQLColumns(StatementHandle,
                      catalog.data(),
                      catalog.smallLength(),
                      schema.data(),
                      schema.smallLength(),
                      table.data(),
                      table.smallLength(),
                      column.data(),
                      column.smallLength());
  });
}

SQLRETURN SQL_API SQLStatisticsW(SQLHSTMT StatementHandle,
                                 SQLWCHAR* CatalogName,
                                 SQLSMALLINT NameLength1,
                                 SQLWCHAR* SchemaName,
                                 SQLSMALLINT NameLength2,
                                 SQLWCHAR* TableName,
                                 SQLSMALLINT NameLength3,
                                 SQLUSMALLINT Unique,
                                 SQLUSMALLINT Reserved) {
  WriteLog(LL_TRACE, "Entering SQLStatisticsW");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  return guardWideCall(statement, "SQLStatisticsW", [&]() {
    NarrowInput catalog(CatalogName, NameLength1, wideTextCodec(statement));
    NarrowInput schema(SchemaName, NameLength2, wideTextCodec(statement));
    NarrowInput table(TableName, NameLength3, wideTextCodec(statement));
    return SQLStatistics(StatementHandle,
                         catalog.data(),
                         catalog.smallLength(),
                         schema.data(),
                         schema.smallLength(),
                         table.data(),
                         table.smallLength(),
                         Unique,
                         Reserved);
  });
}

SQLRETURN SQL_API SQLGetTypeInfoW(SQLHSTMT StatementHandle,
                                  SQLSMALLINT DataType) {
  WriteLog(LL_TRACE, "Entering SQLGetTypeInfoW");
  return SQLGetTypeInfo(StatementHandle, DataType);
}
