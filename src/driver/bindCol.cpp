#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <climits>
#include <string>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLBindCol(SQLHSTMT StatementHandle,
                             SQLUSMALLINT ColumnNumber,
                             SQLSMALLINT TargetType,
                             _Inout_updates_opt_(_Inexpressible_(BufferLength))
                                 SQLPOINTER TargetValuePtr,
                             SQLLEN BufferLength,
                             _Inout_opt_ SQLLEN* StrLen_or_Ind) {
  /*
  What is this thing?
  Typically called before executing the SQL statement or fetching any rows.
  You bind the columns to application variables ahead of time, so when
  SQLFetch is called, the data is automatically placed in the bound
  variables.

  TargetType here refers to the C data type of the buffer where
  this is allocated. That's not the same as the SQL type.

  Note that TargetValuePtr may be a null pointer. If so, this effectively
  "unbinds" the column, since SQLFetch will treat descriptors with a null
  TargetValuePtr as not being bound.
  */

  WriteLog(LL_TRACE, "Entering SQLBindCol");
  WriteLog(LL_TRACE, "  Column Number is: " + std::to_string(ColumnNumber));
  WriteLog(LL_TRACE, "  Target Type is: " + std::to_string(TargetType));
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  if (ColumnNumber < 1 || ColumnNumber >= SHRT_MAX) {
    // Bookmarks (column 0) are not supported.
    statement->setError(ErrorInfo("Invalid descriptor index", "07009"));
    return SQL_ERROR;
  }
  if (TargetValuePtr &&
      (TargetType == SQL_C_CHAR || TargetType == SQL_C_WCHAR ||
       TargetType == SQL_C_BINARY) &&
      BufferLength < 0) {
    statement->setError(ErrorInfo("Invalid string or buffer length", "HY090"));
    return SQL_ERROR;
  }
  DescriptorField field = statement->getRowDescriptor()->getField(ColumnNumber);
  field.bufferCDataType = TargetType;
  field.bufferPtr       = TargetValuePtr;
  field.bufferLength    = BufferLength;
  field.bufferStrLenOrIndPtr = StrLen_or_Ind;
  statement->getRowDescriptor()->setField(ColumnNumber, field);
  return SQL_SUCCESS;
}
