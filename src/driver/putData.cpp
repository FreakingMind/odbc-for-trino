#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLPutData(SQLHSTMT StatementHandle,
                             _In_reads_(_Inexpressible_(StrLen_or_Ind))
                                 SQLPOINTER Data,
                             SQLLEN StrLen_or_Ind) {
  WriteLog(LL_TRACE, "Entering SQLPutData");
  return reportNotImplemented(StatementHandle, "SQLPutData");
}
