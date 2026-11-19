#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstring>
#include <map>

#include "../trinoAPIWrapper/trinoExceptions.hpp"
#include "../util/valuePtrHelper.hpp"
#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"
#include "mappings/typeMappings.hpp"

SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT StatementHandle,
                                 SQLUSMALLINT ColumnNumber,
                                 _Out_writes_opt_(BufferLength)
                                     SQLCHAR* ColumnName,
                                 SQLSMALLINT BufferLength,
                                 _Out_opt_ SQLSMALLINT* NameLength,
                                 _Out_opt_ SQLSMALLINT* DataType,
                                 _Out_opt_ SQLULEN* ColumnSize,
                                 _Out_opt_ SQLSMALLINT* DecimalDigits,
                                 _Out_opt_ SQLSMALLINT* Nullable) {
  WriteLog(LL_TRACE, "Entering SQLDescribeCol");
  WriteLog(LL_TRACE, "  Column index: " + std::to_string(ColumnNumber));

  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  if (BufferLength < 0) {
    statement->setError(ErrorInfo("Invalid string or buffer length", "HY090"));
    return SQL_ERROR;
  }

  std::vector<ColumnDescription> columnDescriptions;
  try {
    columnDescriptions = statement->trinoQuery->getColumnDescriptions();
  } catch (const std::exception& ex) {
    statement->setError(ErrorInfo("Exception thrown during SQLDescribeCol: " +
                                      std::string(ex.what()),
                                  sqlStateFor(ex)));
    return SQL_ERROR;
  }
  if (columnDescriptions.empty() && statement->trinoQuery->hasError()) {
    statement->reportServerError();
    return SQL_ERROR;
  }
  if (ColumnNumber < 1 || ColumnNumber > columnDescriptions.size()) {
    statement->setError(ErrorInfo("Invalid descriptor index", "07009"));
    return SQL_ERROR;
  }

  const ColumnDescription& thisColumnDescription =
      columnDescriptions.at(ColumnNumber - 1);

  Descriptor* descriptorPtr       = statement->getRowDescriptor();
  DescriptorField descriptorField = descriptorPtr->getField(ColumnNumber);

  const bool truncated = writeNullTermStringToPtr(
      ColumnName, thisColumnDescription.getName(), BufferLength, NameLength);
  if (DataType) {
    *DataType = inferODBCTypeCode(thisColumnDescription);
  }
  if (ColumnSize) {
    // Character types are reported in characters, as required by ODBC.
    *ColumnSize = inferODBCColumnSize(thisColumnDescription);
    WriteLog(LL_TRACE,
             "  ODBC ColumnSize reported as: " + std::to_string(*ColumnSize));
  }
  if (Nullable) {
    // I don't know that trino provides a way to determine this based on
    // the underlying column metadata. It may be possible with additional
    // queries of metadata, but I don't think it's in the metadata
    // that comes with a normal SELECT statement. Assuming everything
    // is nullable seems like a reasonable default.
    *Nullable = SQL_NULLABLE;
  }
  if (DecimalDigits) {
    // The documentation says the value "0" should be set if the number of
    // digits cannot be determined or is not applicable. That's the default
    // value of the descriptorField struct's scale.
    *DecimalDigits = descriptorField.scale;
  }

  if (truncated) {
    statement->setError(ErrorInfo("String data, right truncated", "01004"));
    return SQL_SUCCESS_WITH_INFO;
  }
  return SQL_SUCCESS;
}
