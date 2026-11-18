#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "fetch.hpp"
#include "handles/statementHandle.hpp"

/*
ODBC 2.x block fetch. Driver managers may map SQLFetch to it for ODBC 2
applications. Like SQLFetchScroll, only SQL_FETCH_NEXT with one row per call
is supported (forward-only cursor, rowset size 1).
*/
SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT hstmt,
                                   SQLUSMALLINT fFetchType,
                                   SQLLEN irow,
                                   _Out_opt_ SQLULEN* pcrow,
                                   _Out_opt_ SQLUSMALLINT* rgfRowStatus) {
  WriteLog(LL_TRACE, "Entering SQLExtendedFetch");
  if (!hstmt) {
    WriteLog(LL_ERROR, "  ERROR: Invalid statement handle");
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(hstmt);
  statement->clearDiagnostics();
  if (fFetchType != SQL_FETCH_NEXT) {
    WriteLog(LL_ERROR,
             "  ERROR: Unsupported fetch type " + std::to_string(fFetchType) +
                 "; only SQL_FETCH_NEXT is supported");
    statement->setError(ErrorInfo("Fetch type out of range", "HY106"));
    return SQL_ERROR;
  }
  static_cast<void>(irow);

  const SQLRETURN ret = fetchNextRow(statement);
  const bool gotRow   = statement->hasCurrentRow();
  if (pcrow) {
    *pcrow = gotRow ? 1 : 0;
  }
  if (rgfRowStatus && gotRow) {
    rgfRowStatus[0] = ret == SQL_SUCCESS             ? SQL_ROW_SUCCESS
                      : ret == SQL_SUCCESS_WITH_INFO ? SQL_ROW_SUCCESS_WITH_INFO
                                                     : SQL_ROW_ERROR;
  }
  return ret;
}
