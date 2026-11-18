#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLParamData(SQLHSTMT StatementHandle,
                               _Out_opt_ SQLPOINTER* Value) {
  WriteLog(LL_TRACE, "Entering SQLParamData");
  return reportNotImplemented(StatementHandle, "SQLParamData");
}
