#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/writeLog.hpp"
#include "constants/statementAttrs.hpp"
#include "handles/statementHandle.hpp"

namespace {
template <typename T>
void writeAttr(SQLPOINTER Value, SQLINTEGER* StringLength, T value) {
  if (Value) {
    *reinterpret_cast<T*>(Value) = value;
  }
  if (StringLength) {
    *StringLength = static_cast<SQLINTEGER>(sizeof(T));
  }
}
} // namespace

SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT StatementHandle,
                                 SQLINTEGER Attribute,
                                 _Out_writes_opt_(_Inexpressible_(BufferLength))
                                     SQLPOINTER Value,
                                 SQLINTEGER BufferLength,
                                 _Out_opt_ SQLINTEGER* StringLength) {
  WriteLog(LL_TRACE, "Entering SQLGetStmtAttr");
  if (!StatementHandle) {
    WriteLog(LL_ERROR, "  ERROR: Invalid statement handle");
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();
  // All attributes the driver reports are fixed-size; BufferLength is
  // ignored for them.
  static_cast<void>(BufferLength);

  switch (Attribute) {
    case SQL_ATTR_ROW_NUMBER: { // 14
      // 1-based number of the current row, 0 when there's none.
      const SQLULEN rowNumber =
          statement->hasCurrentRow()
              ? static_cast<SQLULEN>(statement->getFetchedPosition() + 1)
              : 0;
      writeAttr<SQLULEN>(Value, StringLength, rowNumber);
      break;
    }
    case SQL_ATTR_APP_ROW_DESC: { // 10010
      writeAttr<SQLPOINTER>(Value, StringLength, statement->appRowDesc);
      break;
    }
    case SQL_ATTR_APP_PARAM_DESC: { // 10011
      writeAttr<SQLPOINTER>(Value, StringLength, statement->appParamDesc);
      break;
    }
    case SQL_ATTR_IMP_ROW_DESC: { // 10012
      writeAttr<SQLPOINTER>(Value, StringLength, statement->impRowDesc);
      break;
    }
    case SQL_ATTR_IMP_PARAM_DESC: { // 10013
      writeAttr<SQLPOINTER>(Value, StringLength, statement->impParamDesc);
      break;
    }
    case SQL_ATTR_ROWS_FETCHED_PTR: { // 26
      writeAttr<SQLPOINTER>(
          Value, StringLength, statement->impRowDesc->Field_RowsProcessedPtr);
      break;
    }
    case SQL_ATTR_ROW_STATUS_PTR: { // 25
      writeAttr<SQLPOINTER>(
          Value, StringLength, statement->impRowDesc->Field_ArrayStatusPtr);
      break;
    }
    case SQL_ATTR_ROW_BIND_OFFSET_PTR: { // 23
      writeAttr<SQLPOINTER>(
          Value, StringLength, statement->appRowDesc->Field_BindOffsetPtr);
      break;
    }
    case SQL_ATTR_ROW_BIND_TYPE: { // 5
      writeAttr<SQLULEN>(
          Value, StringLength, statement->appRowDesc->Field_BindType);
      break;
    }
    case SQL_ATTR_ROW_ARRAY_SIZE:  // 27
    case SQL_ROWSET_SIZE:          // 9
    case SQL_ATTR_PARAMSET_SIZE: { // 22
      writeAttr<SQLULEN>(Value, StringLength, 1);
      break;
    }
    case SQL_ATTR_CURSOR_TYPE: { // 6
      writeAttr<SQLULEN>(Value, StringLength, SQL_CURSOR_FORWARD_ONLY);
      break;
    }
    case SQL_ATTR_CONCURRENCY: { // 7
      writeAttr<SQLULEN>(Value, StringLength, SQL_CONCUR_READ_ONLY);
      break;
    }
    case SQL_ATTR_CURSOR_SCROLLABLE: { // -1
      writeAttr<SQLULEN>(Value, StringLength, SQL_NONSCROLLABLE);
      break;
    }
    case SQL_ATTR_CURSOR_SENSITIVITY: { // -2
      writeAttr<SQLULEN>(Value, StringLength, SQL_UNSPECIFIED);
      break;
    }
    case SQL_ATTR_ASYNC_ENABLE: { // 4
      writeAttr<SQLULEN>(Value, StringLength, SQL_ASYNC_ENABLE_OFF);
      break;
    }
    case SQL_ATTR_RETRIEVE_DATA: { // 11
      writeAttr<SQLULEN>(Value, StringLength, SQL_RD_ON);
      break;
    }
    case SQL_ATTR_USE_BOOKMARKS: { // 12
      writeAttr<SQLULEN>(Value, StringLength, SQL_UB_OFF);
      break;
    }
    case SQL_ATTR_QUERY_TIMEOUT: { // 0
      writeAttr<SQLULEN>(Value, StringLength, statement->queryTimeout);
      break;
    }
    case SQL_ATTR_MAX_ROWS: { // 1
      writeAttr<SQLULEN>(Value, StringLength, statement->maxRows);
      break;
    }
    case SQL_ATTR_MAX_LENGTH: { // 3
      writeAttr<SQLULEN>(Value, StringLength, statement->maxLength);
      break;
    }
    case SQL_ATTR_NOSCAN: { // 2
      writeAttr<SQLULEN>(Value, StringLength, statement->noScan);
      break;
    }
    case SQL_ATTR_METADATA_ID: { // 10014
      writeAttr<SQLULEN>(Value, StringLength, statement->metadataId);
      break;
    }
    case SQL_ATTR_RAW_STATEMENT_HANDLE: { // 1001
      // Driver defined - get raw statement handle, no ODBC proxy.
      writeAttr<SQLPOINTER>(Value, StringLength, StatementHandle);
      break;
    }
    default: {
      WriteLog(LL_ERROR,
               "  ERROR: Unsupported attribute: " + std::to_string(Attribute));
      if (StringLength) {
        *StringLength = 0;
      }
      statement->setError(
          ErrorInfo("Invalid attribute/option identifier", "HY092"));
      return SQL_ERROR;
    }
  }
  WriteLog(LL_TRACE,
           "  Finished getting attribute: " + std::to_string(Attribute));
  return SQL_SUCCESS;
}
