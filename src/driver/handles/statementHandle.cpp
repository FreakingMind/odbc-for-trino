#include "statementHandle.hpp"
#include <functional>
#include <string>
#include <utility>

#include "../mappings/typeMappings.hpp"

#include "../../util/writeLog.hpp"

void Statement::columnsChangedCallback(TrinoQuery* trinoQuery) {
  /*
  It's important to update the row descriptors whenever the query returns
  information about the column contents.
  */
  Descriptor* rowDescriptor = this->getRowDescriptor();
  // Use 1-based indexing, since column 0 is used to hold the bookmark data.
  int i = 1;
  for (const ColumnDescription& colDescription :
       trinoQuery->getColumnDescriptions()) {
    DescriptorField field           = rowDescriptor->getField(i);
    const std::string& trinoRawType = colDescription.getRawType();
    field.columnName                = colDescription.getName();
    field.trinoRawTypeName          = trinoRawType;
    // TODO: How do we find out what fields are actually nullable from Trino?
    field.nullable = true;
    if (colDescription.getRawType() == "decimal") {
      // Decimal precision and scale are the values of the first and second
      // type arguments. Decimal types use a numerical precision radix of 10,
      // but that is set by the TRINO_RAW_TYPE_TO_NUM_PREC_RADIX lookup.
      field.precision =
          colDescription.getTypeArguments()[0]["value"].get<SQLCHAR>();
      field.scale =
          colDescription.getTypeArguments()[1]["value"].get<SQLCHAR>();
    }
    // Not all types have precision, but if they do, set it.
    if (TRINO_RAW_TYPE_TO_PRECISION.contains(trinoRawType)) {
      field.precision = TRINO_RAW_TYPE_TO_PRECISION.at(trinoRawType);
    }
    // Not all types have a numerical precision radix, but if they do, set it.
    if (TRINO_RAW_TYPE_TO_NUM_PREC_RADIX.contains(trinoRawType)) {
      field.numPrecRadix = TRINO_RAW_TYPE_TO_NUM_PREC_RADIX.at(trinoRawType);
    }
    try {
      // This is always true for integral types! Trino doesn't support unsigned
      // integral types. It internally converts unsigned types to the next
      // larger signed integral type. uint64s are turned to decimals, which
      // finishes the job.
      field.isUnsigned = TRINO_RAW_TYPE_TO_UNSIGNED.at(trinoRawType);
    } catch (const std::exception& ex) {
      WriteLog(LL_ERROR,
               "ERROR: Key " + trinoRawType +
                   " not found in unsigned lookup: " + ex.what());
    }
    field.odbcDataType = inferODBCTypeCode(colDescription);
    field.length = static_cast<SQLINTEGER>(inferODBCColumnSize(colDescription));
    field.octetLength = inferODBCOctetLength(colDescription);
    WriteLog(LL_TRACE,
             "  Column metadata: name=" + field.columnName + ", type=" +
                 trinoRawType + ", length=" + std::to_string(field.length) +
                 ", octetLength=" + std::to_string(field.octetLength));
    this->getRowDescriptor()->setField(i, field);
    i++;
  }
}

Statement::Statement(ConnectionConfig* connectionConfig) {
  this->connectionConfig = connectionConfig;
  this->trinoQuery       = new TrinoQuery(connectionConfig);
  this->impParamDesc     = new Descriptor();
  this->impRowDesc       = new Descriptor();
  // Application descriptors are managed by the application,
  // and thus the driver is not responsible for allocating
  // them or managing their memory.
  this->appParamDesc = this->impParamDesc;
  this->appRowDesc   = this->impRowDesc;

  // Register a callback for times when column data has changed.
  this->trinoQuery->registerColumnDataChangeCallback(std::bind(
      &Statement::columnsChangedCallback, this, std::placeholders::_1));

  // Register a callback to reset cursor position when a new query is posted.
  this->trinoQuery->registerPostCallback([this](TrinoQuery*) {
    this->fetchedPosition = -1;
    this->rowAvailable    = false;
    this->resetGetDataState();
  });
}

Statement::~Statement() {
  delete this->trinoQuery;
  // We can free memory for the implementation descriptors,
  // but should not free the application descriptors because
  // those are allocated by and managed by the application.
  delete this->impParamDesc;
  delete this->impRowDesc;
}

/*
Reset gets this statement ready to be used again with a different query and
different bindings.
*/
void Statement::reset() {
  this->closeCursor();
  this->impParamDesc->reset();
  this->impRowDesc->reset();
}

/*
Closing the cursor discards the result set only. The prepared statement (its
name lives on the TrinoQuery and survives TrinoQuery::reset()) and the column
and parameter bindings stay, so the application can call SQLExecute again
and fetch into the same buffers.
*/
void Statement::closeCursor() {
  // Stop a query that is still running so it doesn't keep consuming
  // resources on the server. A failure here must not prevent closing.
  this->terminate();
  const std::string query = this->trinoQuery->getQuery();
  this->trinoQuery->reset();
  this->trinoQuery->setQuery(query);
  this->executed              = false;
  this->fetchExecuteConfirmed = false;
  this->fetchedPosition       = -1;
  this->rowAvailable          = false;
  this->serverErrorReported   = false;
  this->resetGetDataState();
}

void Statement::unbindColumns() {
  this->appRowDesc->unbindAll();
}

void Statement::resetParams() {
  this->appParamDesc->reset();
}

/*
Terminate stops an in-flight query immediately. It also
gets called just before freeing the statement handle as
a saftey mechanism to make sure any in-flight queries
aren't left hanging after the statement is freed.
*/
bool Statement::terminate() {
  try {
    this->trinoQuery->terminate();
  } catch (const std::exception& ex) {
    WriteLog(LL_WARN,
             "  WARNING: Could not terminate the Trino query: " +
                 std::string(ex.what()));
    return false;
  }
  return true;
}

/*
If the application provides their own descriptor,
we will ignore the default one we are providing
within the driver.
*/
Descriptor* Statement::getRowDescriptor() {
  if (this->appRowDesc) {
    return this->appRowDesc;
  } else {
    return this->impRowDesc;
  }
}

Descriptor* Statement::getParamDescriptor() {
  /*
  If the application provides their own descriptor,
  we will ignore the default one we are providing
  within the driver.
  */
  if (this->appParamDesc) {
    return this->appParamDesc;
  } else {
    return this->impParamDesc;
  }
}

SQLLEN Statement::getFetchedPosition() {
  return this->fetchedPosition;
}

void Statement::setFetchedPosition(SQLLEN pos) {
  if (this->fetchedPosition != pos) {
    this->resetGetDataState();
  }
  this->fetchedPosition = pos;
}

void Statement::resetGetDataState() {
  this->getDataState = GetDataState{};
}

void Statement::prepareGetData(SQLLEN row,
                               SQLUSMALLINT column,
                               SQLSMALLINT cDataType,
                               std::size_t initialByteOffset) {
  if (!this->getDataState.active || this->getDataState.row != row ||
      this->getDataState.column != column ||
      this->getDataState.cDataType != cDataType) {
    this->getDataState.active       = true;
    this->getDataState.complete     = false;
    this->getDataState.row          = row;
    this->getDataState.column       = column;
    this->getDataState.cDataType    = cDataType;
    this->getDataState.byteOffset   = initialByteOffset;
    this->getDataState.hasAnsiValue = false;
    this->getDataState.ansiValue.clear();
  }
}

const std::string* Statement::findGetDataAnsiValue(SQLLEN row,
                                                   SQLUSMALLINT column) const {
  const GetDataState& state = this->getDataState;
  if (state.active && state.hasAnsiValue && state.row == row &&
      state.column == column && state.cDataType == SQL_C_CHAR) {
    return &state.ansiValue;
  }
  return nullptr;
}

const std::string& Statement::storeGetDataAnsiValue(std::string value) {
  this->getDataState.ansiValue    = std::move(value);
  this->getDataState.hasAnsiValue = true;
  return this->getDataState.ansiValue;
}

std::size_t Statement::getDataByteOffset() const {
  return this->getDataState.byteOffset;
}

bool Statement::getDataIsComplete() const {
  return this->getDataState.active && this->getDataState.complete;
}

void Statement::advanceGetData(std::size_t bytesCopied, bool complete) {
  this->getDataState.byteOffset += bytesCopied;
  this->getDataState.complete = complete;
}

bool Statement::hasCurrentRow() const {
  return this->rowAvailable;
}

void Statement::setHasCurrentRow(bool value) {
  this->rowAvailable = value;
}

void Statement::setError(ErrorInfo errorInfo) {
  this->errorInfo = errorInfo;
}

void Statement::clearDiagnostics() {
  this->errorInfo           = ErrorInfo();
  this->serverErrorReported = false;
}

void Statement::reportServerError() {
  this->serverErrorReported = true;
}

bool Statement::hasServerError() const {
  return this->serverErrorReported && this->trinoQuery->hasError();
}

ErrorInfo Statement::getError() {
  return this->errorInfo;
}

void Statement::setQueryId(const std::string& id) {
  queryId = id;
}

SQLRETURN reportNotImplemented(SQLHSTMT statementHandle,
                               const char* functionName) {
  WriteLog(LL_ERROR,
           "  ERROR: " + std::string(functionName) + " is unimplemented");
  if (!statementHandle) {
    return SQL_INVALID_HANDLE;
  }
  Statement* statement = reinterpret_cast<Statement*>(statementHandle);
  statement->clearDiagnostics();
  statement->setError(ErrorInfo(std::string(functionName) +
                                    ": optional feature not implemented",
                                "HYC00"));
  return SQL_ERROR;
}
