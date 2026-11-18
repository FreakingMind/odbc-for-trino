#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT StatementHandle, SQLUSMALLINT Option) {
  WriteLog(LL_TRACE, "Entering SQLFreeStmt");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* stmt = reinterpret_cast<Statement*>(StatementHandle);
  if (Option == SQL_DROP) {
    WriteLog(LL_WARN, "  Closing statement with SQL_DROP (deprecated)");
    return SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle);
  }

  stmt->clearDiagnostics();
  switch (Option) {
    case (SQL_CLOSE): {
      // Unlike SQLCloseCursor, SQL_CLOSE without an open cursor is not an
      // error. Bindings and the prepared statement are kept.
      WriteLog(
          LL_TRACE,
          "  Closing statement with SQL_CLOSE. The statement may be reused.");
      stmt->closeCursor();
      return SQL_SUCCESS;
    }
    case (SQL_UNBIND): {
      WriteLog(LL_TRACE, "  Releasing all column bindings (SQL_UNBIND)");
      stmt->unbindColumns();
      return SQL_SUCCESS;
    }
    case (SQL_RESET_PARAMS): {
      WriteLog(LL_TRACE,
               "  Releasing all parameter bindings (SQL_RESET_PARAMS)");
      stmt->resetParams();
      return SQL_SUCCESS;
    }
    default: {
      WriteLog(LL_ERROR,
               "  ERROR: Unknown option in SQLFreeStmt: " +
                   std::to_string(Option));
      stmt->setError(ErrorInfo("Option type out of range", "HY092"));
      return SQL_ERROR;
    }
  }
}
