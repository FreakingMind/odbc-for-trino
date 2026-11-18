#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <string>

#include "config/profileReader.hpp"
#include "handles/connHandle.hpp"

#include "../util/stringFromChar.hpp"
#include "../util/writeLog.hpp"


SQLRETURN SQL_API SQLConnect(SQLHDBC ConnectionHandle,
                             _In_reads_(NameLength1) SQLCHAR* DSNChars,
                             SQLSMALLINT NameLength1,
                             _In_reads_(NameLength2) SQLCHAR* UserNameChars,
                             SQLSMALLINT NameLength2,
                             _In_reads_(NameLength3)
                                 SQLCHAR* AuthenticationChars,
                             SQLSMALLINT NameLength3) {
  WriteLog(LL_TRACE, "Entering SQLConnect");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  // Exceptions must never escape an ODBC entry point (they would terminate
  // the host application), e.g. for an invalid value in the DSN.
  try {
    std::string dsn     = stringFromChar(DSNChars, NameLength1);
    DriverConfig config = readDriverConfigFromProfile(dsn);

    // It's kind of unfortunate that we can't set the log level of the driver
    // until we've read the DSN in some way.
    WriteLog(LL_TRACE, "  Setting Log Level");
    setLogLevel(config.getLogLevelEnum());

    WriteLog(LL_TRACE, "  Configuring connection");
    connection->configure(config);
    connection->connected = true;
  } catch (const std::exception& e) {
    WriteLog(LL_ERROR, "  ERROR: SQLConnect failed: " + std::string(e.what()));
    connection->setError(ErrorInfo(e.what(), "HY000"));
    return SQL_ERROR;
  }
  return SQL_SUCCESS;
}
