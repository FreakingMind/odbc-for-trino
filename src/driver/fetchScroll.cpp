#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

/*
The driver exposes forward-only cursors, so only SQL_FETCH_NEXT is
supported. Driver managers translate SQLFetch into
SQLFetchScroll(SQL_FETCH_NEXT) for ODBC 3 applications (iODBC does, which is
what Excel for Mac uses), so this must behave exactly like SQLFetch.
*/
SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT StatementHandle,
                                 SQLSMALLINT FetchOrientation,
                                 SQLLEN FetchOffset) {
  WriteLog(LL_TRACE, "Entering SQLFetchScroll");
  if (!StatementHandle) {
    WriteLog(LL_ERROR, "  ERROR: Invalid statement handle");
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  if (FetchOrientation != SQL_FETCH_NEXT) {
    WriteLog(LL_ERROR,
             "  ERROR: Unsupported fetch orientation " +
                 std::to_string(FetchOrientation) +
                 "; only SQL_FETCH_NEXT is supported");
    statement->setError(ErrorInfo("Fetch type out of range", "HY106"));
    return SQL_ERROR;
  }
  // FetchOffset is ignored for SQL_FETCH_NEXT.
  static_cast<void>(FetchOffset);
  return SQLFetch(StatementHandle);
}
