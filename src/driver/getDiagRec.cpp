#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "handles/connHandle.hpp"
#include "handles/descriptorHandle.hpp"
#include "handles/envHandle.hpp"
#include "handles/statementHandle.hpp"

#include "../util/valuePtrHelper.hpp"
#include "../util/writeLog.hpp"

namespace {

void writeSqlState(SQLCHAR* SqlStatePtr, const std::string& sqlState) {
  if (SqlStatePtr) {
    // SQLSTATE is always 5 characters plus the terminator.
    std::string state = sqlState.substr(0, 5);
    state.resize(5, '0');
    std::memcpy(SqlStatePtr, state.c_str(), 6);
  }
}

// Writes one diagnostic record. The message is truncated to BufferLength
// (including the terminator) and TextLengthPtr always gets the full length,
// as the ODBC spec requires; truncation returns SQL_SUCCESS_WITH_INFO.
SQLRETURN copyDiagnostic(const std::string& sqlState,
                         SQLINTEGER nativeError,
                         const std::string& message,
                         SQLCHAR* SqlStatePtr,
                         SQLINTEGER* NativeErrorPtr,
                         SQLCHAR* MessageTextPtr,
                         SQLSMALLINT BufferLength,
                         SQLSMALLINT* TextLengthPtr) {
  writeSqlState(SqlStatePtr, sqlState);
  if (NativeErrorPtr) {
    *NativeErrorPtr = nativeError;
  }
  const bool truncated = writeNullTermStringToPtr(
      MessageTextPtr, message, BufferLength, TextLengthPtr);
  return truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
}

// Length of the longest prefix of text that is at most maxBytes long and
// doesn't end in the middle of a UTF-8 character.
size_t utf8Prefix(std::string_view text, size_t maxBytes) {
  if (text.size() <= maxBytes) {
    return text.size();
  }
  size_t length = maxBytes;
  while (length > 0 &&
         (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
    --length;
  }
  return length > 0 ? length : maxBytes;
}

/*
Trino errors (summary plus stack trace) can be much longer than the
application's message buffer. Rather than truncating, they're split into
several diagnostic records at line boundaries: each record starts with a
newline and holds as many complete lines as fit into BufferLength. A line
longer than that is split.
*/
std::vector<std::string> chunkLines(const std::vector<std::string>& lines,
                                    size_t capacity) {
  std::vector<std::string> chunks;
  std::string current = "\n";
  auto flush          = [&]() {
    if (current.size() > 1) {
      chunks.push_back(current);
    }
    current = "\n";
  };
  for (const std::string& line : lines) {
    std::string_view rest = line;
    do {
      const size_t room = capacity - current.size();
      if (rest.size() + 1 <= room) {
        current.append(rest);
        current += '\n';
        rest = {};
        break;
      }
      if (current.size() > 1) {
        flush();
        continue;
      }
      // The line doesn't fit into an empty record either.
      const size_t take = utf8Prefix(rest, room - 1);
      current.append(rest.substr(0, take));
      current += '\n';
      rest.remove_prefix(take);
      flush();
    } while (!rest.empty());
  }
  flush();
  return chunks;
}

SQLRETURN copyServerDiagnostic(const TrinoOdbcErrorHandler::OdbcError& odbcErr,
                               SQLSMALLINT RecNumber,
                               SQLCHAR* SqlStatePtr,
                               SQLINTEGER* NativeErrorPtr,
                               SQLCHAR* MessageTextPtr,
                               SQLSMALLINT BufferLength,
                               SQLSMALLINT* TextLengthPtr) {
  // Build all lines: summary (split by newlines) + stack (each with tab)
  std::vector<std::string> lines;
  {
    std::istringstream summaryStream(
        TrinoOdbcErrorHandler::OdbcErrorToString(odbcErr, false));
    std::string line;
    while (std::getline(summaryStream, line)) {
      lines.push_back(line);
    }
  }
  for (const auto& entry : odbcErr.stack) {
    lines.push_back("\t" + entry);
  }

  // A buffer too small to hold even a short line (or none at all, when the
  // application asks for the length first): the whole message is record 1.
  constexpr SQLSMALLINT MIN_CHUNK_BUFFER = 4;
  if (!MessageTextPtr || BufferLength < MIN_CHUNK_BUFFER) {
    if (RecNumber != 1) {
      return SQL_NO_DATA;
    }
    std::string message = "\n";
    for (const std::string& line : lines) {
      message += line + "\n";
    }
    return copyDiagnostic(odbcErr.sqlstate,
                          odbcErr.native,
                          message,
                          SqlStatePtr,
                          NativeErrorPtr,
                          MessageTextPtr,
                          BufferLength,
                          TextLengthPtr);
  }

  const std::vector<std::string> chunks =
      chunkLines(lines, static_cast<size_t>(BufferLength) - 1);
  if (static_cast<size_t>(RecNumber) > chunks.size()) {
    return SQL_NO_DATA;
  }
  return copyDiagnostic(odbcErr.sqlstate,
                        odbcErr.native,
                        chunks[static_cast<size_t>(RecNumber) - 1],
                        SqlStatePtr,
                        NativeErrorPtr,
                        MessageTextPtr,
                        BufferLength,
                        TextLengthPtr);
}

} // namespace

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT HandleType,
                                SQLHANDLE Handle,
                                SQLSMALLINT RecNumber,
                                _Out_writes_opt_(6) SQLCHAR* SqlStatePtr,
                                SQLINTEGER* NativeErrorPtr,
                                _Out_writes_opt_(BufferLength)
                                    SQLCHAR* MessageTextPtr,
                                SQLSMALLINT BufferLength,
                                _Out_opt_ SQLSMALLINT* TextLengthPtr) {
  /*
  Return a series of 1-indexed diagnostic records from various handles.
  If a record is requested beyond what is actually available, return
  SQL_NO_DATA instead.
  */
  WriteLog(LL_DEBUG,
           "Entering SQLGetDiagRec. HandleType= " + std::to_string(HandleType) +
               ", RecNumber= " + std::to_string(RecNumber));
  if (!Handle) {
    return SQL_INVALID_HANDLE;
  }
  // SQLGetDiagRec doesn't post diagnostics about itself.
  if (RecNumber <= 0 || BufferLength < 0) {
    WriteLog(LL_ERROR,
             "  ERROR: SQLGetDiagRec called with an invalid RecNumber or "
             "BufferLength");
    return SQL_ERROR;
  }

  switch (HandleType) {
    case (SQL_HANDLE_ENV): {
      return SQL_NO_DATA;
    }
    case (SQL_HANDLE_DBC): {
      Connection* conn    = reinterpret_cast<Connection*>(Handle);
      ErrorInfo errorInfo = conn->getError();
      if (RecNumber != 1 || !errorInfo.errorOccurred()) {
        return SQL_NO_DATA;
      }
      return copyDiagnostic(errorInfo.sqlStateCode,
                            -1,
                            errorInfo.errorMessage,
                            SqlStatePtr,
                            NativeErrorPtr,
                            MessageTextPtr,
                            BufferLength,
                            TextLengthPtr);
    }
    case (SQL_HANDLE_STMT): {
      Statement* statement = reinterpret_cast<Statement*>(Handle);

      // Errors reported by Trino for the query of the last function call.
      if (statement->hasServerError()) {
        return copyServerDiagnostic(statement->trinoQuery->getError(),
                                    RecNumber,
                                    SqlStatePtr,
                                    NativeErrorPtr,
                                    MessageTextPtr,
                                    BufferLength,
                                    TextLengthPtr);
      }

      // Errors raised by the driver itself (network/TLS failures, rejected
      // credentials, unsupported conversions, truncation warnings, ...).
      ErrorInfo driverError = statement->getError();
      if (RecNumber != 1 || !driverError.errorOccurred()) {
        return SQL_NO_DATA;
      }
      return copyDiagnostic(driverError.sqlStateCode,
                            0,
                            driverError.errorMessage,
                            SqlStatePtr,
                            NativeErrorPtr,
                            MessageTextPtr,
                            BufferLength,
                            TextLengthPtr);
    }
    case (SQL_HANDLE_DESC): {
      return SQL_NO_DATA;
    }
    default: {
      WriteLog(LL_ERROR, "  ERROR: Unknown handle type in SQLGetDiagRec");
      return SQL_ERROR;
    }
  }
}
