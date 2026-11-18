#pragma once

#include "../../util/windowsLean.hpp"
#include <mutex>
#include <sql.h>
#include <sqlext.h>
#include <vector>

#include "../../platform/textEncoding.hpp"
#include "../../trinoAPIWrapper/connectionConfig.hpp"
#include "../config/driverConfig.hpp"
#include "handleErrorInfo.hpp"
#include "statementHandle.hpp"

class Connection {
  private:
    std::vector<Statement*> statements;
    std::mutex statementsMutex;
    void freeAllStatements();
    EnvironmentConfig* environmentConfig = nullptr;
    ErrorInfo errorInfo;

  public:
    Connection(EnvironmentConfig* environmentConfig);
    ~Connection();
    void configure(DriverConfig config);
    bool connected                     = false;
    ConnectionConfig* connectionConfig = nullptr;
    // The narrow encoding of SQL_C_CHAR data (ansiEncoding option),
    // resolved by configure() and copied to each new statement.
    AnsiEncoding ansiEncoding = utf8AnsiEncoding();
    // SQL_ATTR_CURRENT_CATALOG set before connecting: overrides the DSN's
    // catalog when the connection is configured.
    std::string pendingCatalog;
    // The session's current catalog ("" when none is set).
    std::string getCurrentCatalog();
    void setCurrentCatalog(const std::string& catalog);
    void disconnect();

    // Statements allocated on this connection and not freed yet. The ODBC
    // spec has SQLDisconnect free them; iODBC relies on that.
    void registerStatement(Statement* statement);
    void unregisterStatement(Statement* statement);

    SQLINTEGER ATTR_AutoCommitMode = SQL_AUTOCOMMIT_ON;
    SQLUINTEGER ATTR_LoginTimeout  = 0;

    std::string getServerVersion();
    void setError(ErrorInfo errorInfo);
    ErrorInfo getError();

    void checkInputs(DriverConfig config);
};
