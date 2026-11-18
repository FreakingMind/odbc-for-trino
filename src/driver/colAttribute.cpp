#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <string>

#include "../util/valuePtrHelper.hpp"
#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"
#include "mappings/typeMappings.hpp"

namespace {
void writeNumericAttribute(SQLPOINTER destination, SQLLEN value) {
  if (destination) {
    *reinterpret_cast<SQLLEN*>(destination) = value;
  }
}
} // namespace

#pragma warning(push)
/*
The CharacterAttributePtr and NumericAttributePtr fields may
be unused if they aren't relevant to the requested attribute/column.
This is a documented expectation for these out parameters, so we'll
disable the warning about returning uninitialized memory in an out
parameter.
https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlcolattribute-function
*/
#pragma warning(disable : 6101)

/*
 The signature of this function differs between 64-bit and 32-bit
 targets. For 32-bit Windows, NumericAttributePtr is a SQLPOINTER, but for
 64-bit Windows, its a SQLLEN*. iODBC and unixODBC always declare SQLLEN*.
 The definition must match the header's declaration exactly, otherwise it
 is compiled as a C++ overload and the C entry point is never exported.
 */
#if defined(_WIN64) || !defined(_WIN32)
SQLRETURN SQL_API SQLColAttribute(SQLHSTMT StatementHandle,
                                  SQLUSMALLINT ColumnNumber,
                                  SQLUSMALLINT FieldIdentifier,
                                  _Out_writes_bytes_opt_(BufferLength)
                                      SQLPOINTER CharacterAttributePtr,
                                  SQLSMALLINT BufferLength,
                                  _Out_opt_ SQLSMALLINT* StringLengthPtr,
                                  _Out_opt_ SQLLEN* NumericAttributePtr) {
#else
SQLRETURN SQL_API SQLColAttribute(SQLHSTMT StatementHandle,
                                  SQLUSMALLINT ColumnNumber,
                                  SQLUSMALLINT FieldIdentifier,
                                  _Out_writes_bytes_opt_(BufferLength)
                                      SQLPOINTER CharacterAttributePtr,
                                  SQLSMALLINT BufferLength,
                                  _Out_opt_ SQLSMALLINT* StringLengthPtr,
                                  _Out_opt_ SQLPOINTER NumericAttributePtr) {
#endif
#pragma warning(pop)
  WriteLog(LL_TRACE, "Entering SQLColAttribute");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();

  // The column metadata arrives with the first responses from Trino; make
  // sure it's there (SQLColAttribute may be the first call after execute).
  SQLSMALLINT columnCount = 0;
  try {
    columnCount = static_cast<SQLSMALLINT>(
        statement->trinoQuery->getColumnDescriptions().size());
  } catch (const std::exception& ex) {
    statement->setError(ErrorInfo("Exception thrown during SQLColAttribute: " +
                                      std::string(ex.what()),
                                  "HY000"));
    return SQL_ERROR;
  }
  if (columnCount == 0 && statement->trinoQuery->hasError()) {
    statement->reportServerError();
    return SQL_ERROR;
  }

  if (FieldIdentifier == SQL_DESC_COUNT ||
      FieldIdentifier == SQL_COLUMN_COUNT) {
    // A header field: ColumnNumber is ignored.
    writeNumericAttribute(NumericAttributePtr, columnCount);
    return SQL_SUCCESS;
  }

  if (ColumnNumber < 1 || ColumnNumber > columnCount) {
    statement->setError(ErrorInfo("Invalid descriptor index", "07009"));
    return SQL_ERROR;
  }

  Descriptor* ird            = statement->impRowDesc;
  DescriptorField columnInfo = ird->getField(ColumnNumber);

  // Character attributes are copied with the caller's BufferLength (bytes).
  bool truncated   = false;
  auto writeString = [&](const std::string& value) {
    truncated |= writeNullTermStringToPtr(
        CharacterAttributePtr, value, BufferLength, StringLengthPtr);
  };

  switch (FieldIdentifier) {
    case SQL_DESC_CONCISE_TYPE: { // 2
      WriteLog(LL_TRACE, "  Getting SQL column type");
      SQLSMALLINT odbcTypeCode =
          TRINO_RAW_TYPE_TO_ODBC_TYPE_CODE[columnInfo.trinoRawTypeName];
      writeNumericAttribute(NumericAttributePtr, odbcTypeCode);
      break;
    }
    case SQL_DESC_UNSIGNED: { // 8
      // Trino does not support unsigned integer types, they're
      // always mapped to the next larger type. In the case of
      // unsigned int64s, they are mapped to decimals.
      WriteLog(LL_TRACE, "  Getting SQL column signed-nessness");
      writeNumericAttribute(NumericAttributePtr, columnInfo.isUnsigned);
      break;
    }
    case SQL_COLUMN_TYPE_NAME: { // 14
      WriteLog(LL_TRACE, "  Getting SQL column name");
      std::string columnTypeName = columnInfo.trinoRawTypeName;
      writeString(columnTypeName);
      break;
    }
    case SQL_DESC_NUM_PREC_RADIX: { // 32
      WriteLog(LL_TRACE, "  Getting SQL column attribute NumPrecRadix");
      writeNumericAttribute(NumericAttributePtr, columnInfo.numPrecRadix);
      break;
    }
    case SQL_DESC_DISPLAY_SIZE: { // 6
      WriteLog(LL_TRACE, "  Getting SQL column display size");
      writeNumericAttribute(NumericAttributePtr, columnInfo.length);
      break;
    }
    case SQL_DESC_LENGTH: { // 1003
      WriteLog(LL_TRACE, "  Getting SQL column length");
      // How do we handle non-fixed length varchars?
      // For now, this defaults to SQL_NO_TOTAL (-4).
      writeNumericAttribute(NumericAttributePtr, columnInfo.length);
      break;
    }
    case SQL_DESC_PRECISION: { // 1005
      WriteLog(LL_TRACE, "  Getting SQL column precision");
      writeNumericAttribute(NumericAttributePtr, columnInfo.precision);
      break;
    }
    case SQL_DESC_SCALE: { // 1006
      WriteLog(LL_TRACE, "  Getting SQL column scale");
      writeNumericAttribute(NumericAttributePtr, columnInfo.scale);
      break;
    }
    case SQL_DESC_NULLABLE: { // 1008
      WriteLog(LL_TRACE, "  Getting SQL column null-abilty");
      writeNumericAttribute(NumericAttributePtr, columnInfo.nullable);
      break;
    }
    case SQL_DESC_NAME: { // 1011
      WriteLog(LL_TRACE, "  Getting SQL column name");
      std::string columnName = columnInfo.columnName;
      writeString(columnName);
      break;
    }
    case SQL_DESC_UNNAMED: { // 1012
      WriteLog(LL_TRACE, "  Getting SQL column named-ness");
      writeNumericAttribute(NumericAttributePtr, columnInfo.named);
      break;
    }
    case SQL_DESC_OCTET_LENGTH: { // 1013
      WriteLog(LL_TRACE, "  Getting SQL column octet length");
      writeNumericAttribute(NumericAttributePtr, columnInfo.octetLength);
      break;
    }
    default: {
      WriteLog(LL_ERROR,
               "  ERROR: Unhandled Column attribute type: " +
                   std::to_string(FieldIdentifier));
      statement->setError(
          ErrorInfo("Invalid descriptor field identifier", "HY091"));
      return SQL_ERROR;
    }
  }

  if (truncated) {
    statement->setError(ErrorInfo("String data, right truncated", "01004"));
    return SQL_SUCCESS_WITH_INFO;
  }
  return SQL_SUCCESS;
}
