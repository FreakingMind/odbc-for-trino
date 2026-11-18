#pragma once
#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstddef>
#include <functional>
#include <string>

#include "descriptorHandle.hpp"
#include "handleErrorInfo.hpp"

#include "../../platform/textEncoding.hpp"
#include "../../trinoAPIWrapper/connectionConfig.hpp"
#include "../../trinoAPIWrapper/trinoQuery.hpp"

class Connection;

class Statement {
  private:
    void columnsChangedCallback(TrinoQuery* trinoQuery);
    // SQLFetch needs us to keep track of our position in iterating
    // the results so subsequent calls to fetch can return data
    // starting at the right place. We start at position -1, which
    // indicates that no rows are ready to process.
    SQLLEN fetchedPosition = -1;
    ErrorInfo errorInfo;

    struct GetDataState {
        bool active            = false;
        bool complete          = false;
        SQLLEN row             = -1;
        SQLUSMALLINT column    = 0;
        SQLSMALLINT cDataType  = 0;
        std::size_t byteOffset = 0;
        // SQL_C_CHAR: the value converted to ansiEncoding once, then read in
        // parts (valid while active for this row and column).
        bool hasAnsiValue = false;
        std::string ansiValue;
    };

    GetDataState getDataState;

    // Is the cursor positioned on a row that SQLGetData can read? False
    // before the first SQLFetch, after SQL_NO_DATA and after closing.
    bool rowAvailable = false;

    // Trino reports query failures in a response to a poll. They're kept on
    // the TrinoQuery until the next query is posted, but ODBC wants
    // diagnostics to describe only the most recent function call. This
    // flag says whether the TrinoQuery error belongs to the diagnostics of
    // the current call.
    bool serverErrorReported = false;

    // Trino Query ID
    std::string queryId;

    ConnectionConfig* connectionConfig = nullptr;

  public:
    // The connection that allocated this statement (it frees statements
    // that are still alive on SQLDisconnect).
    Connection* ownerConnection = nullptr;
    // The narrow encoding of SQL_C_CHAR data: the connection's ansiEncoding.
    AnsiEncoding ansiEncoding = utf8AnsiEncoding();
    Statement(ConnectionConfig* connectionConfig);
    ~Statement();
    // Has this statement been executed?
    bool executed = false;
    // Did we confirm execution of this statement to SQLFetch?
    bool fetchExecuteConfirmed = false;
    // The underlying trino query utility class.
    TrinoQuery* trinoQuery;
    // The method used in SQLFetch for polling trino.
    TrinoQueryPollMode fetchPollMode = UntilNewData;

    // The ODBC protocol assumes these descriptors are
    // instantiated on all statements.
    Descriptor* appRowDesc;
    Descriptor* impRowDesc;
    Descriptor* appParamDesc;
    Descriptor* impParamDesc;

    // Statement attributes that the driver accepts and reports back. The
    // cursor is forward-only and read-only and the rowset size is always 1.
    SQLULEN queryTimeout = 0;
    SQLULEN maxRows      = 0;
    SQLULEN maxLength    = 0;
    SQLULEN noScan       = SQL_NOSCAN_OFF;
    SQLULEN metadataId   = SQL_FALSE;

    // Full reset: closes the cursor and drops all column and parameter
    // bindings.
    void reset();
    // SQLCloseCursor / SQLFreeStmt(SQL_CLOSE): stops the running query and
    // discards the result set, but keeps the prepared statement and all
    // column and parameter bindings so the statement can be executed again.
    void closeCursor();
    // SQLFreeStmt(SQL_UNBIND): releases all column bindings.
    void unbindColumns();
    // SQLFreeStmt(SQL_RESET_PARAMS): releases all parameter bindings.
    void resetParams();
    // Stops an in-flight query. Never throws: a failure is logged and
    // reported through the return value.
    bool terminate();
    Descriptor* getRowDescriptor();
    Descriptor* getParamDescriptor();
    SQLLEN getFetchedPosition();
    void setFetchedPosition(SQLLEN pos);

    // SQLGetData can retrieve variable-length data in several calls. These
    // methods keep track of how many bytes have already been returned for the
    // current row, column, and target C type.
    void resetGetDataState();
    void prepareGetData(SQLLEN row,
                        SQLUSMALLINT column,
                        SQLSMALLINT cDataType,
                        std::size_t initialByteOffset);
    std::size_t getDataByteOffset() const;
    // The SQL_C_CHAR value of this row and column converted by an earlier
    // SQLGetData part, or nullptr.
    const std::string* findGetDataAnsiValue(SQLLEN row,
                                            SQLUSMALLINT column) const;
    // Keeps the converted value for the next parts; call after
    // prepareGetData for the same row and column.
    const std::string& storeGetDataAnsiValue(std::string value);
    bool getDataIsComplete() const;
    void advanceGetData(std::size_t bytesCopied, bool complete);

    bool hasCurrentRow() const;
    void setHasCurrentRow(bool value);

    void setError(ErrorInfo errorInfo);
    ErrorInfo getError();
    // ODBC clears a handle's diagnostics at the start of every function
    // called on it, except the diagnostic functions themselves.
    void clearDiagnostics();
    // Makes the error returned by Trino part of the current diagnostics.
    void reportServerError();
    bool hasServerError() const;

    void setQueryId(const std::string& id);
};

// Records HYC00 (optional feature not implemented) on a statement handle
// for an ODBC function the driver doesn't implement, and returns SQL_ERROR
// (SQL_INVALID_HANDLE for a null handle).
SQLRETURN reportNotImplemented(SQLHSTMT statementHandle,
                               const char* functionName);
