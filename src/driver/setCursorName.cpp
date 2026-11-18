#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLSetCursorName(SQLHSTMT StatementHandle,
                                   _In_reads_(NameLength) SQLCHAR* CursorName,
                                   SQLSMALLINT NameLength) {
  WriteLog(LL_TRACE, "Entering SQLSetCursorName");
  return reportNotImplemented(StatementHandle, "SQLSetCursorName");
}
