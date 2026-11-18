#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

/*
Closes the open cursor and discards pending results, like
SQLFreeStmt(SQL_CLOSE). The prepared statement and all column and parameter
bindings are kept.

Without an open cursor the ODBC spec asks for 24000. The Windows driver
manager checks that itself before calling the driver, but iODBC (Excel for
Mac) passes every call through, and applications such as iodbctest close
defensively, sometimes twice in a row. Treating that as a successful no-op
is the more robust choice for those hosts.
*/
SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT StatementHandle) {
  WriteLog(LL_TRACE, "Entering SQLCloseCursor");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  if (!statement->executed) {
    WriteLog(LL_DEBUG,
             "  SQLCloseCursor called without an open cursor; nothing to do");
    return SQL_SUCCESS;
  }
  statement->closeCursor();
  return SQL_SUCCESS;
}
