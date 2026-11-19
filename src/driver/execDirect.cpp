#include "../util/windowsLean.hpp"
#include <sql.h>
#include <string.h>

#include "../trinoAPIWrapper/trinoExceptions.hpp"
#include "../trinoAPIWrapper/trinoQuery.hpp"
#include "../util/stringFromChar.hpp"
#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT StatementHandle,
                                _In_reads_opt_(TextLength)
                                    SQLCHAR* StatementText,
                                SQLINTEGER TextLength) {
  WriteLog(LL_TRACE, "Entering SQLExecDirect");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statementPtr = reinterpret_cast<Statement*>(StatementHandle);
  statementPtr->clearDiagnostics();

  if (not StatementText) {
    WriteLog(LL_ERROR, " ERROR: No StatementText defined for query");
    statementPtr->setError(ErrorInfo("Invalid use of null pointer", "HY009"));
    return SQL_ERROR;
  }

  try {
    Statement* statement = statementPtr;
    // Executing again discards the previous result set; stop its query if
    // it's still running.
    statement->closeCursor();
    std::string queryText = stringFromChar(StatementText, TextLength);
    WriteLog(LL_DEBUG, "  Query: " + queryText);
    TrinoQuery* trinoQuery = statement->trinoQuery;
    WriteLog(LL_DEBUG, "  Setting Query");
    trinoQuery->setQuery(queryText);
    WriteLog(LL_DEBUG, "  POSTing Query");
    trinoQuery->post();
    if (trinoQuery->hasError()) {
      statement->reportServerError();
      return SQL_ERROR;
    }
    WriteLog(LL_DEBUG, "  Setting to executed");
    statement->executed = true;
    return SQL_SUCCESS;
  } catch (const std::exception& ex) {
    WriteLog(LL_ERROR,
             "  ERROR: Exception thrown during SQLExecDirect: " +
                 std::string(ex.what()));
    // Set the diagnostic record on the statement handle
    // to make it possible to tell what happened here.
    ErrorInfo errorInfo("Exception thrown during SQLExecDirect: " +
                            std::string(ex.what()),
                        sqlStateFor(ex));
    statementPtr->setError(errorInfo);
    return SQL_ERROR;
  }
}
