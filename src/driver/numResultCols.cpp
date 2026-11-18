#include "../util/windowsLean.hpp"
#include <sql.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT StatementHandle,
                                   _Out_ SQLSMALLINT* ColumnCount) {
  WriteLog(LL_TRACE, "Entering SQLNumResultCols");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  if (!ColumnCount) {
    statement->setError(ErrorInfo("Invalid use of null pointer", "HY009"));
    return SQL_ERROR;
  }
  try {
    WriteLog(LL_TRACE, "  Getting Column Count");
    SQLSMALLINT queryColumnCount = statement->trinoQuery->getColumnCount();
    WriteLog(LL_TRACE, "  Got Column Count");
    *ColumnCount = queryColumnCount;
  } catch (const std::exception& ex) {
    WriteLog(LL_ERROR,
             "  ERROR: Exception thrown during SQLNumResultCols: " +
                 std::string(ex.what()));
    statement->setError(ErrorInfo("Exception thrown during SQLNumResultCols: " +
                                      std::string(ex.what()),
                                  "HY000"));
    return SQL_ERROR;
  }
  WriteLog(LL_TRACE,
           "  Result col count is set to: " + std::to_string(*ColumnCount));

  if (statement->trinoQuery->hasError()) {
    statement->reportServerError();
    return SQL_ERROR;
  } else {
    return SQL_SUCCESS;
  }
}
