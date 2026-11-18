#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "../platform/textEncoding.hpp"
#include "../trinoAPIWrapper/columnDescription.hpp"
#include "../util/jsonValueToText.hpp"
#include "../util/rowToBuffer.hpp"
#include "../util/writeLog.hpp"
#include "handles/statementHandle.hpp"

using json = nlohmann::json;

namespace {
// SQLFetch already copied the start of a bound SQL_C_CHAR column (see
// copyStrToBuffer); SQLGetData continues after it.
std::size_t getInitialBoundAnsiOffset(const DescriptorField& descriptorField,
                                      const std::string& value,
                                      SQLSMALLINT requestedCDataType,
                                      AnsiEncoding encoding) {
  if (requestedCDataType != SQL_C_CHAR ||
      descriptorField.bufferPtr == nullptr ||
      descriptorField.bufferCDataType != SQL_C_CHAR ||
      descriptorField.bufferLength <= 0) {
    return 0;
  }

  const SQLLEN copiedByFetch = calculateAnsiChunkLength(
      value, 0, descriptorField.bufferLength - 1, encoding);
  const std::size_t copiedBytes =
      copiedByFetch > 0 ? static_cast<std::size_t>(copiedByFetch) : 0;
  return copiedBytes < value.size() ? copiedBytes : 0;
}

std::size_t getInitialBoundWideOffset(const DescriptorField& descriptorField,
                                      const std::wstring& value,
                                      SQLSMALLINT requestedCDataType) {
  if (requestedCDataType != SQL_C_WCHAR ||
      descriptorField.bufferPtr == nullptr ||
      descriptorField.bufferCDataType != SQL_C_WCHAR ||
      descriptorField.bufferLength < static_cast<SQLLEN>(sizeof(SQLWCHAR))) {
    return 0;
  }

  const SQLLEN payloadCapacity =
      descriptorField.bufferLength - static_cast<SQLLEN>(sizeof(SQLWCHAR));
  const SQLLEN copiedByFetch =
      calculateWideSafeCopyLengthBytes(value, 0, payloadCapacity);
  const std::size_t copiedBytes =
      copiedByFetch > 0 ? static_cast<std::size_t>(copiedByFetch) : 0;
  const std::size_t totalBytes = value.size() * sizeof(SQLWCHAR);
  return copiedBytes < totalBytes ? copiedBytes : 0;
}

// The SQL_C_CHAR value of a column for SQLGetData: converted to the
// statement's ansiEncoding on the first part and kept for the next ones.
const std::string& prepareAnsiValue(Statement* statement,
                                    const json& jsonValue,
                                    SQLUSMALLINT columnNumber,
                                    const DescriptorField& descriptorField) {
  const SQLLEN fetchedPosition = statement->getFetchedPosition();
  if (const std::string* cached =
          statement->findGetDataAnsiValue(fetchedPosition, columnNumber)) {
    return *cached;
  }

  const AnsiEncoding encoding = statement->ansiEncoding;
  bool replaced               = false;
  std::string value =
      utf8ToAnsi(jsonValueToText(jsonValue), encoding, &replaced);
  const std::size_t initialOffset =
      getInitialBoundAnsiOffset(descriptorField, value, SQL_C_CHAR, encoding);
  // A bound SQL_C_CHAR column was converted (and warned about) by SQLFetch.
  const bool boundAsChar = descriptorField.bufferPtr != nullptr &&
                           descriptorField.bufferCDataType == SQL_C_CHAR;
  if (replaced && !boundAsChar) {
    WriteLog(LL_WARN,
             "  WARNING: Characters unavailable in the narrow encoding (" +
                 ansiEncodingName(encoding) + ") were replaced with ?");
  }

  statement->prepareGetData(
      fetchedPosition, columnNumber, SQL_C_CHAR, initialOffset);
  return statement->storeGetDataAnsiValue(std::move(value));
}

// Copies the next part of value (from prepareAnsiValue).
SQLRETURN copyAnsiDataPart(Statement* statement,
                           const std::string& value,
                           SQLPOINTER buffer,
                           SQLLEN bufferLength,
                           SQLLEN* strLen_or_IndPtr) {
  const AnsiEncoding encoding = statement->ansiEncoding;
  if (statement->getDataIsComplete()) {
    WriteLog(LL_TRACE, "  SQLGetData has no more ANSI data for this column");
    return SQL_NO_DATA;
  }

  const std::size_t byteOffset = statement->getDataByteOffset();
  const std::size_t remainingBytes =
      byteOffset < value.size() ? value.size() - byteOffset : 0;

  if (strLen_or_IndPtr) {
    *strLen_or_IndPtr = static_cast<SQLLEN>(remainingBytes);
  }

  const SQLLEN payloadCapacity = bufferLength > 0 ? bufferLength - 1 : 0;
  SQLLEN copyLength            = 0;
  if (buffer && payloadCapacity > 0 && remainingBytes > 0) {
    copyLength =
        calculateAnsiChunkLength(value, byteOffset, payloadCapacity, encoding);
  }

  if (buffer && copyLength > 0) {
    std::memcpy(static_cast<char*>(buffer),
                value.data() + byteOffset,
                static_cast<std::size_t>(copyLength));
  }
  if (buffer && bufferLength > 0) {
    static_cast<char*>(buffer)[copyLength] = '\0';
  }

  const std::size_t copiedBytes =
      copyLength > 0 ? static_cast<std::size_t>(copyLength) : 0;
  const bool complete = copiedBytes >= remainingBytes;
  statement->advanceGetData(copiedBytes, complete);

  if (!complete) {
    statement->setError(ErrorInfo("String data, right truncated", "01004"));
    WriteLog(LL_TRACE,
             "  SQLGetData returned partial ANSI data: offset=" +
                 std::to_string(byteOffset) +
                 ", copied=" + std::to_string(copiedBytes) +
                 ", remaining_before_call=" + std::to_string(remainingBytes));
    return SQL_SUCCESS_WITH_INFO;
  }

  WriteLog(
      LL_TRACE,
      "  SQLGetData completed ANSI data: offset=" + std::to_string(byteOffset) +
          ", copied=" + std::to_string(copiedBytes));
  return SQL_SUCCESS;
}

SQLRETURN copyWideDataPart(Statement* statement,
                           const std::wstring& value,
                           SQLUSMALLINT columnNumber,
                           const DescriptorField& descriptorField,
                           SQLPOINTER buffer,
                           SQLLEN bufferLength,
                           SQLLEN* strLen_or_IndPtr) {
  const SQLLEN fetchedPosition = statement->getFetchedPosition();
  const std::size_t initialOffset =
      getInitialBoundWideOffset(descriptorField, value, SQL_C_WCHAR);

  statement->prepareGetData(
      fetchedPosition, columnNumber, SQL_C_WCHAR, initialOffset);

  if (statement->getDataIsComplete()) {
    WriteLog(LL_TRACE, "  SQLGetData has no more wide data for this column");
    return SQL_NO_DATA;
  }

  const std::size_t totalBytes = value.size() * sizeof(SQLWCHAR);
  const std::size_t byteOffset = statement->getDataByteOffset();
  const std::size_t remainingBytes =
      byteOffset < totalBytes ? totalBytes - byteOffset : 0;

  if (strLen_or_IndPtr) {
    *strLen_or_IndPtr = static_cast<SQLLEN>(remainingBytes);
  }

  const SQLLEN terminatorBytes = sizeof(SQLWCHAR);
  const SQLLEN payloadCapacity =
      bufferLength >= terminatorBytes ? bufferLength - terminatorBytes : 0;
  SQLLEN copyBytes = 0;
  if (buffer && payloadCapacity > 0 && remainingBytes > 0) {
    copyBytes =
        calculateWideSafeCopyLengthBytes(value, byteOffset, payloadCapacity);
  }

  if (buffer && copyBytes > 0) {
    const auto* sourceBytes =
        reinterpret_cast<const unsigned char*>(value.data());
    std::memcpy(
        buffer, sourceBytes + byteOffset, static_cast<std::size_t>(copyBytes));
  }
  if (buffer && bufferLength >= terminatorBytes) {
    const std::size_t terminatorIndex =
        static_cast<std::size_t>(copyBytes) / sizeof(SQLWCHAR);
    static_cast<SQLWCHAR*>(buffer)[terminatorIndex] = 0;
  }

  const std::size_t copiedBytes =
      copyBytes > 0 ? static_cast<std::size_t>(copyBytes) : 0;
  const bool complete = copiedBytes >= remainingBytes;
  statement->advanceGetData(copiedBytes, complete);

  if (!complete) {
    statement->setError(ErrorInfo("String data, right truncated", "01004"));
    WriteLog(LL_TRACE,
             "  SQLGetData returned partial WCHAR data: offset=" +
                 std::to_string(byteOffset) +
                 ", copied=" + std::to_string(copiedBytes) +
                 ", remaining_before_call=" + std::to_string(remainingBytes));
    return SQL_SUCCESS_WITH_INFO;
  }

  WriteLog(LL_TRACE,
           "  SQLGetData completed WCHAR data: offset=" +
               std::to_string(byteOffset) +
               ", copied=" + std::to_string(copiedBytes));
  return SQL_SUCCESS;
}
} // namespace

SQLRETURN SQL_API SQLGetData(SQLHSTMT StatementHandle,
                             SQLUSMALLINT columnNumber,
                             SQLSMALLINT cDataType,
                             _Out_writes_opt_(_Inexpressible_(bufferLength))
                                 SQLPOINTER buffer,
                             SQLLEN bufferLength,
                             _Out_opt_ SQLLEN* strLen_or_IndPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetData");

  if (!StatementHandle) {
    WriteLog(LL_ERROR, "  ERROR: Invalid statement handle");
    return SQL_INVALID_HANDLE;
  }

  Statement* statement = reinterpret_cast<Statement*>(StatementHandle);
  statement->clearDiagnostics();

  if (!statement->hasCurrentRow()) {
    // Before the first SQLFetch, after SQL_NO_DATA or after closing the
    // cursor there is no row to read from.
    WriteLog(LL_ERROR, "  ERROR: SQLGetData - The cursor is not on a row");
    statement->setError(ErrorInfo("Invalid cursor state", "24000"));
    return SQL_ERROR;
  }

  // BufferLength only matters for variable-length C types; the ODBC spec
  // says it is ignored for fixed-length ones (applications pass anything).
  const bool variableLengthCType = cDataType == SQL_C_CHAR ||
                                   cDataType == SQL_C_WCHAR ||
                                   cDataType == SQL_C_BINARY;
  if (variableLengthCType && bufferLength < 0) {
    WriteLog(LL_ERROR, "  ERROR: SQLGetData - Invalid buffer length");
    statement->setError(ErrorInfo("Invalid string or buffer length", "HY090"));
    return SQL_ERROR;
  }

  const std::vector<ColumnDescription>& columnDescriptions =
      statement->trinoQuery->getColumnDescriptions();

  if (columnNumber < 1 || columnNumber > columnDescriptions.size()) {
    WriteLog(LL_ERROR, "  ERROR: SQLGetData - Column out of bounds");
    if (strLen_or_IndPtr) {
      *strLen_or_IndPtr = SQL_NULL_DATA;
    }
    statement->setError(ErrorInfo("Invalid descriptor index", "07009"));
    return SQL_ERROR;
  }

  Descriptor* rowDescriptor = statement->getRowDescriptor();
  const ColumnDescription& thisColumnDescription =
      columnDescriptions.at(columnNumber - 1);
  const DescriptorField descriptorField = rowDescriptor->getField(columnNumber);
  const SQLSMALLINT odbcDataType        = descriptorField.odbcDataType;

  const SQLLEN fetchedPosition = statement->getFetchedPosition();
  const json& rowData = statement->trinoQuery->getRowAtIndex(fetchedPosition);

  if (getLogLevel() <= LL_TRACE) {
    WriteLog(LL_TRACE,
             "  Getting data for column: " + thisColumnDescription.getName());
    WriteLog(LL_TRACE, "  CDataType is: " + std::to_string(cDataType));
  }

  if (rowData[columnNumber - 1].is_null()) {
    statement->prepareGetData(fetchedPosition, columnNumber, cDataType, 0);
    if (statement->getDataIsComplete()) {
      return SQL_NO_DATA;
    }
    if (!strLen_or_IndPtr) {
      statement->setError(
          ErrorInfo("Indicator variable required but not supplied", "22002"));
      return SQL_ERROR;
    }
    *strLen_or_IndPtr = SQL_NULL_DATA;
    statement->advanceGetData(0, true);
    return SQL_SUCCESS;
  }

  if (cDataType == SQL_C_CHAR || cDataType == SQL_C_WCHAR) {
    try {
      if (cDataType == SQL_C_WCHAR) {
        return copyWideDataPart(
            statement,
            utf8ToWide(jsonValueToText(rowData[columnNumber - 1])),
            columnNumber,
            descriptorField,
            buffer,
            bufferLength,
            strLen_or_IndPtr);
      }
      const std::string& ansiValue = prepareAnsiValue(
          statement, rowData[columnNumber - 1], columnNumber, descriptorField);
      return copyAnsiDataPart(
          statement, ansiValue, buffer, bufferLength, strLen_or_IndPtr);
    } catch (const std::exception& e) {
      WriteLog(LL_ERROR,
               "  ERROR: extracting character value for column index: " +
                   std::to_string(columnNumber) + " - " + e.what());
      statement->setError(
          ErrorInfo("Restricted data type attribute violation", "07006"));
      return SQL_ERROR;
    }
  }

  statement->prepareGetData(fetchedPosition, columnNumber, cDataType, 0);
  if (statement->getDataIsComplete()) {
    return SQL_NO_DATA;
  }

  ColumnToBufferStatus status = columnToBuffer(cDataType,
                                               odbcDataType,
                                               rowData,
                                               columnNumber,
                                               buffer,
                                               bufferLength,
                                               strLen_or_IndPtr,
                                               descriptorField.precision,
                                               descriptorField.scale,
                                               statement->ansiEncoding);

  if (!status.isSuccess) {
    statement->setError(ErrorInfo(status.errorMessage, status.sqlState));
    return SQL_ERROR;
  }

  statement->advanceGetData(0, true);
  return status.isTruncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
}
