#include "unicodeWrappers.hpp"

#include <string>

#include "../../util/writeLog.hpp"
#include "../handles/connHandle.hpp"
#include "../handles/statementHandle.hpp"

namespace {

const char* const TRUNCATION_MESSAGE = "String data, right truncated";
const char* const TRUNCATION_STATE   = "01004";

SQLRETURN withTruncationInfo(SQLRETURN rc, bool truncated) {
  if (truncated && rc == SQL_SUCCESS) {
    return SQL_SUCCESS_WITH_INFO;
  }
  return rc;
}

} // namespace

WideTextCodec wideTextCodec(const Connection* connection) {
  if (connection == nullptr) {
    return {};
  }
  if (!connection->connected) {
    // Before the connection knows its ansiEncoding (connection strings,
    // connect errors): what "auto" gives this application, the encoding
    // Excel uses. The host application doesn't change.
    static const WideTextCodec unconnected =
        wideTextCodecFor(resolveAnsiEncoding("auto"));
    return unconnected;
  }
  return wideTextCodecFor(connection->ansiEncoding);
}

WideTextCodec wideTextCodec(const Statement* statement) {
  if (statement == nullptr) {
    return {};
  }
  return wideTextCodecFor(statement->ansiEncoding);
}

WideTextCodec wideTextCodec(SQLSMALLINT handleType, SQLHANDLE handle) {
  switch (handleType) {
    case SQL_HANDLE_DBC:
      return wideTextCodec(static_cast<const Connection*>(handle));
    case SQL_HANDLE_STMT:
      return wideTextCodec(static_cast<const Statement*>(handle));
    default:
      return {};
  }
}

SQLRETURN reportTruncation(Statement* statement, SQLRETURN rc, bool truncated) {
  // A warning the ANSI call returned stays the diagnostic record.
  if (truncated && rc == SQL_SUCCESS && statement) {
    statement->setError(ErrorInfo(TRUNCATION_MESSAGE, TRUNCATION_STATE));
  }
  return withTruncationInfo(rc, truncated);
}

SQLRETURN
reportTruncation(Connection* connection, SQLRETURN rc, bool truncated) {
  if (truncated && rc == SQL_SUCCESS && connection) {
    connection->setError(ErrorInfo(TRUNCATION_MESSAGE, TRUNCATION_STATE));
  }
  return withTruncationInfo(rc, truncated);
}

SQLRETURN reportTruncation(SQLRETURN rc, bool truncated) {
  return withTruncationInfo(rc, truncated);
}

void logWideCallException(const char* functionName, const char* what) noexcept {
  try {
    WriteLog(LL_ERROR,
             "  ERROR: " + std::string(functionName) +
                 " failed: " + std::string(what));
  } catch (...) {
    // Logging must not throw out of an ODBC entry point.
  }
}

namespace {

ErrorInfo exceptionError(const char* functionName, const char* what) {
  return ErrorInfo(std::string(functionName) + ": " + std::string(what),
                   "HY000");
}

} // namespace

void recordWideCallException(Statement* statement,
                             const char* functionName,
                             const char* what) noexcept {
  try {
    statement->setError(exceptionError(functionName, what));
  } catch (...) {
    // Out of memory: the SQL_ERROR return code is all we can report.
  }
}

void recordWideCallException(Connection* connection,
                             const char* functionName,
                             const char* what) noexcept {
  try {
    connection->setError(exceptionError(functionName, what));
  } catch (...) {
    // Out of memory: the SQL_ERROR return code is all we can report.
  }
}
