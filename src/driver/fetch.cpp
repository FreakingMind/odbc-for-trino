#include "../util/windowsLean.hpp"
#include <sql.h>

#include <cstdint>
#include <string>

#include "fetch.hpp"

#include "../trinoAPIWrapper/trinoQuery.hpp"
#include "../util/rowToBuffer.hpp"
#include "../util/writeLog.hpp"
#include "handles/descriptorHandle.hpp"
#include "handles/statementHandle.hpp"

namespace {

template <typename T> T* applyBindOffset(T* ptr, SQLLEN offset) {
  if (ptr == nullptr || offset == 0) {
    return ptr;
  }
  return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr) + offset);
}

SQLRETURN handleBoundColumns(Statement* statement) {
  /*
  Every time we fetch a row, we need to check if any of the data
  that was returned is from a column that has been bound to a
  buffer. If it has, we need to copy the data directly into
  the buffer before returning from the call to SQLFetch().
  */

  // First, make sure we have column information. We can't do anything with
  // bound columns until we know what columns we have.
  if (not statement->trinoQuery->hasColumnData()) {
    statement->trinoQuery->poll(UntilColumnsLoaded);
  }

  int16_t columnCount       = statement->trinoQuery->getColumnCount();
  Descriptor* rowDescriptor = statement->getRowDescriptor();
  SQLLEN fetchedPosition    = statement->getFetchedPosition();
  const json& rowData = statement->trinoQuery->getRowAtIndex(fetchedPosition);
  // SQL_ATTR_ROW_BIND_OFFSET_PTR: added to every bound data and
  // length/indicator address.
  const SQLLEN bindOffset      = rowDescriptor->Field_BindOffsetPtr
                                     ? *rowDescriptor->Field_BindOffsetPtr
                                     : 0;
  const SQLSMALLINT fieldCount = rowDescriptor->getFieldCount();

  // Field indices start at 1 because index 0 is the "bookmark" column.
  for (SQLSMALLINT i = 1; i <= columnCount && i < fieldCount; i++) {
    const DescriptorField& field = rowDescriptor->getFieldRef(i);

    // If the column isn't bound, there's nothing to be done.
    if (field.bufferPtr == nullptr) {
      continue;
    }

    SQLLEN* strLen_or_IndPtr =
        applyBindOffset(field.bufferStrLenOrIndPtr, bindOffset);
    void* buffer             = applyBindOffset(field.bufferPtr, bindOffset);
    SQLLEN bufferLength      = field.bufferLength;
    SQLSMALLINT cDataType    = field.bufferCDataType;
    SQLSMALLINT odbcDataType = field.odbcDataType;
    SQLULEN columnNumber     = static_cast<SQLULEN>(i);

    // This is in a tight loop, so best to not even execute it
    // if there's a chance of skipping the calls to std::to_string.
    if (getLogLevel() <= LL_TRACE) {
      WriteLog(LL_TRACE, "  Bound column detected. Writing value");
      WriteLog(LL_TRACE, "  ODBC Type is: " + std::to_string(odbcDataType));
      WriteLog(LL_TRACE, "  C Type is: " + std::to_string(cDataType));
    }

    ColumnToBufferStatus status = columnToBuffer(cDataType,
                                                 odbcDataType,
                                                 rowData,
                                                 columnNumber,
                                                 buffer,
                                                 bufferLength,
                                                 strLen_or_IndPtr,
                                                 field.precision,
                                                 field.scale,
                                                 statement->ansiEncoding);
    if (!status.isSuccess) {
      statement->setError(
          ErrorInfo(status.errorMessage + " (column " + std::to_string(i) + ")",
                    status.sqlState));
      return SQL_ERROR;
    }
    if (status.isTruncated) {
      // Excel/Power Query binds a small preview buffer (commonly 100
      // characters), inspects the full untruncated length through the
      // indicator pointer, and then calls SQLGetData for the remainder.
      // Returning SQL_SUCCESS_WITH_INFO here makes Power Query keep only the
      // preview buffer and skip the SQLGetData continuation.  Keep SQLFetch
      // successful while preserving the full length in StrLen_or_IndPtr; the
      // SQLGetData implementation will continue after the bytes/UTF-16 units
      // already copied by this fetch.
      WriteLog(LL_TRACE,
               "  Bound character column exceeded the application buffer; "
               "full length was reported and SQLFetch returns SQL_SUCCESS "
               "so Excel/Power Query can continue with SQLGetData");
    }
  }
  return SQL_SUCCESS;
}

} // namespace

SQLRETURN fetchNextRow(Statement* statement) {
  TrinoQuery* trinoQuery = statement->trinoQuery;

  if (!statement->executed) {
    WriteLog(LL_ERROR, "  ERROR: Fetch called before the statement executed");
    statement->setHasCurrentRow(false);
    statement->setError(ErrorInfo("Function sequence error", "HY010"));
    return SQL_ERROR;
  }

  /*
  A call to Fetch can basically result in 4 possible flows of execution.
  This is a summary of what needs to happen.

  1. The query is done, no rows remain
    * Checkpoint the trino query to clear the row cache.
    * return SQL_NO_DATA, or SQL_ERROR if Trino reported that the query
      failed.
  2. Rows remain available to advance to.
    * Advance by one row
    * return SQL_SUCCESS;
  3. The query is not done, but no rows were available to advance to.
    * Checkpoint the trino query to clear the row cache.
    * Poll for more data.
    * Start the fetch over again.
  4. Some kind of error - report and abort
    * return SQL_ERROR;
  */
  try {
    while (true) {
      const bool trinoQueryCompleted   = trinoQuery->getIsCompleted();
      const int64_t trinoQueryRowCount = trinoQuery->getCurrentRowCount();
      const SQLLEN fetchedPosition     = statement->getFetchedPosition();

      if (statement->maxRows > 0 &&
          fetchedPosition + 1 >= static_cast<SQLLEN>(statement->maxRows)) {
        WriteLog(LL_TRACE, "  SQL_ATTR_MAX_ROWS reached");
        statement->setHasCurrentRow(false);
        return SQL_NO_DATA;
      }

      if (fetchedPosition < (trinoQueryRowCount - 1)) {
        // Handle the case that data is waiting to be read.
        WriteLog(LL_TRACE,
                 "  There are more rows to read. Advancing row pointer.");
        statement->setFetchedPosition(fetchedPosition + 1);
        statement->setHasCurrentRow(true);
        return handleBoundColumns(statement);
      }

      // No buffered rows remain. Free the memory of the rows already read.
      trinoQuery->checkpointRowPosition(fetchedPosition);

      if (trinoQueryCompleted) {
        statement->setHasCurrentRow(false);
        if (trinoQuery->hasError()) {
          WriteLog(LL_ERROR, "  SQLFetch: the Trino query failed");
          statement->reportServerError();
          return SQL_ERROR;
        }
        WriteLog(LL_TRACE, "  SQLFetch is indicating that no data remains");
        return SQL_NO_DATA;
      }

      // The query is not yet completed: poll Trino for more data. By
      // default, the poll mode is UntilNewData.
      WriteLog(LL_TRACE, "  Trino query not completed. Polling until new data");
      trinoQuery->poll(statement->fetchPollMode);
      WriteLog(LL_TRACE,
               "  Trino poll complete. Got row count: " +
                   std::to_string(trinoQuery->getCurrentRowCount()));
    }
  } catch (const std::exception& ex) {
    WriteLog(LL_ERROR,
             "  ERROR: Exception thrown during SQLFetch: " +
                 std::string(ex.what()));
    statement->setError(
        ErrorInfo("Exception thrown during SQLFetch: " + std::string(ex.what()),
                  "HY000"));
    return SQL_ERROR;
  }
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT StatementHandle) {
  WriteLog(LL_TRACE, "Entering SQLFetch");
  if (!StatementHandle) {
    WriteLog(LL_ERROR, "  ERROR: Invalid statement handle");
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();

  const SQLRETURN ret = fetchNextRow(statement);

  // The rowset size is always 1: SQL_ATTR_ROWS_FETCHED_PTR gets the number
  // of rows fetched by this call (error rows included), not a position.
  const bool gotRow = statement->hasCurrentRow();
  Descriptor* ird   = statement->impRowDesc;
  if (ird->Field_RowsProcessedPtr) {
    *(ird->Field_RowsProcessedPtr) = gotRow ? 1 : 0;
  }
  if (ird->Field_ArrayStatusPtr && ret != SQL_NO_DATA) {
    SQLUSMALLINT rowStatus = SQL_ROW_NOROW;
    if (gotRow) {
      rowStatus = ret == SQL_SUCCESS             ? SQL_ROW_SUCCESS
                  : ret == SQL_SUCCESS_WITH_INFO ? SQL_ROW_SUCCESS_WITH_INFO
                                                 : SQL_ROW_ERROR;
    }
    ird->Field_ArrayStatusPtr[0] = rowStatus;
  }
  return ret;
}
