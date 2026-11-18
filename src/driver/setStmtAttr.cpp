#include "../util/windowsLean.hpp"
#include <format>
#include <sql.h>
#include <sqlext.h>

#include "../trinoAPIWrapper/trinoQuery.hpp"
#include "../util/writeLog.hpp"
#include "constants/statementAttrs.hpp"
#include "handles/statementHandle.hpp"

namespace {
// Integer attributes are passed by value in the pointer argument.
SQLULEN integerValue(SQLPOINTER Value) {
  return static_cast<SQLULEN>(reinterpret_cast<uintptr_t>(Value));
}

// The driver supports one value of the attribute. Any other value is
// replaced by it, which ODBC reports as 01S02 (option value changed).
SQLRETURN
acceptOnly(Statement* statement, SQLULEN requested, SQLULEN supported) {
  if (requested == supported) {
    return SQL_SUCCESS;
  }
  WriteLog(LL_DEBUG,
           "  Unsupported attribute value " + std::to_string(requested) +
               ", using " + std::to_string(supported));
  statement->setError(ErrorInfo("Option value changed", "01S02"));
  return SQL_SUCCESS_WITH_INFO;
}
} // namespace

SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT StatementHandle,
                                 SQLINTEGER Attribute,
                                 _In_reads_(_Inexpressible_(StringLength))
                                     SQLPOINTER Value,
                                 SQLINTEGER StringLength) {
  WriteLog(LL_TRACE, "Entering SQLSetStmtAttr");
  if (!StatementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  static_cast<void>(StringLength);

  WriteLog(LL_TRACE, "  Setting attribute: " + std::to_string(Attribute));
  switch (Attribute) {
    case SQL_ATTR_ROWS_FETCHED_PTR: { // 26
      WriteLog(LL_TRACE, std::format("  Attribute value is set to {}", Value));
      statement->impRowDesc->Field_RowsProcessedPtr =
          static_cast<SQLULEN*>(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_ROW_STATUS_PTR: { // 25
      statement->impRowDesc->Field_ArrayStatusPtr =
          static_cast<SQLUSMALLINT*>(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_ROW_BIND_OFFSET_PTR: { // 23
      statement->appRowDesc->Field_BindOffsetPtr = static_cast<SQLLEN*>(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_ROW_BIND_TYPE: { // 5
      // With a rowset size of 1, column-wise and row-wise binding address
      // the same buffers, so any binding type works.
      statement->appRowDesc->Field_BindType =
          static_cast<SQLUINTEGER>(integerValue(Value));
      return SQL_SUCCESS;
    }
    case SQL_ATTR_ROW_ARRAY_SIZE: // 27
    case SQL_ROWSET_SIZE: {       // 9
      // The driver fetches one row at a time.
      return acceptOnly(statement, integerValue(Value), 1);
    }
    case SQL_ATTR_PARAMSET_SIZE: { // 22
      return acceptOnly(statement, integerValue(Value), 1);
    }
    case SQL_ATTR_CURSOR_TYPE: { // 6
      return acceptOnly(
          statement, integerValue(Value), SQL_CURSOR_FORWARD_ONLY);
    }
    case SQL_ATTR_CONCURRENCY: { // 7
      return acceptOnly(statement, integerValue(Value), SQL_CONCUR_READ_ONLY);
    }
    case SQL_ATTR_CURSOR_SCROLLABLE: { // -1
      return acceptOnly(statement, integerValue(Value), SQL_NONSCROLLABLE);
    }
    case SQL_ATTR_CURSOR_SENSITIVITY: { // -2
      return acceptOnly(statement, integerValue(Value), SQL_UNSPECIFIED);
    }
    case SQL_ATTR_ASYNC_ENABLE: { // 4
      return acceptOnly(statement, integerValue(Value), SQL_ASYNC_ENABLE_OFF);
    }
    case SQL_ATTR_RETRIEVE_DATA: { // 11
      return acceptOnly(statement, integerValue(Value), SQL_RD_ON);
    }
    case SQL_ATTR_USE_BOOKMARKS: { // 12
      return acceptOnly(statement, integerValue(Value), SQL_UB_OFF);
    }
    case SQL_ATTR_QUERY_TIMEOUT: { // 0
      // Accepted and reported back; Trino's own query limits apply.
      statement->queryTimeout = integerValue(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_MAX_ROWS: { // 1
      statement->maxRows = integerValue(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_MAX_LENGTH: { // 3
      statement->maxLength = integerValue(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_NOSCAN: { // 2
      statement->noScan = integerValue(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_METADATA_ID: { // 10014
      statement->metadataId = integerValue(Value);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_DEFAULT_FETCH_POLL_MODE: { // 1002
      // Driver-defined; the value is passed by reference.
      if (!Value) {
        statement->setError(ErrorInfo("Invalid use of null pointer", "HY009"));
        return SQL_ERROR;
      }
      SQLINTEGER pollModeInt = *reinterpret_cast<SQLINTEGER*>(Value);
      WriteLog(LL_TRACE,
               "  Attribute value is set to " + std::to_string(pollModeInt));
      statement->fetchPollMode = static_cast<TrinoQueryPollMode>(pollModeInt);
      return SQL_SUCCESS;
    }
    case SQL_ATTR_APP_ROW_DESC:          // 10010
    case SQL_ATTR_APP_PARAM_DESC:        // 10011
    case SQL_ATTR_KEYSET_SIZE:           // 8
    case SQL_ATTR_SIMULATE_CURSOR:       // 10
    case SQL_ATTR_ENABLE_AUTO_IPD:       // 15
    case SQL_ATTR_FETCH_BOOKMARK_PTR:    // 16
    case SQL_ATTR_PARAM_BIND_OFFSET_PTR: // 17
    case SQL_ATTR_PARAM_BIND_TYPE:       // 18
    case SQL_ATTR_PARAM_OPERATION_PTR:   // 19
    case SQL_ATTR_PARAM_STATUS_PTR:      // 20
    case SQL_ATTR_PARAMS_PROCESSED_PTR:  // 21
    case SQL_ATTR_ROW_OPERATION_PTR: {   // 24
      WriteLog(LL_ERROR,
               "  ERROR: Attribute " + std::to_string(Attribute) +
                   " is not supported");
      statement->setError(
          ErrorInfo("Optional feature not implemented", "HYC00"));
      return SQL_ERROR;
    }
    case SQL_ATTR_IMP_ROW_DESC:   // 10012
    case SQL_ATTR_IMP_PARAM_DESC: // 10013
    case SQL_ATTR_ROW_NUMBER: {   // 14
      // Read-only attributes.
      statement->setError(
          ErrorInfo("Invalid attribute/option identifier", "HY092"));
      return SQL_ERROR;
    }
    default: {
      WriteLog(LL_ERROR,
               "  ERROR: Attribute " + std::to_string(Attribute) +
                   " is not implemented");
      statement->setError(
          ErrorInfo("Invalid attribute/option identifier", "HY092"));
      return SQL_ERROR;
    }
  }
}
