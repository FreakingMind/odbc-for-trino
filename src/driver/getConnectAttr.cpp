#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include "../util/valuePtrHelper.hpp"
#include "../util/writeLog.hpp"
#include "handles/connHandle.hpp"


SQLRETURN SQL_API SQLGetConnectAttr(
    SQLHDBC ConnectionHandle,
    SQLINTEGER Attribute,
    _Out_writes_opt_(_Inexpressible_(BufferLength)) SQLPOINTER Value,
    SQLINTEGER BufferLength,
    _Out_opt_ SQLINTEGER* StringLengthPtr) {
  WriteLog(LL_TRACE, "Entering SQLGetConnectAttr");
  if (!ConnectionHandle) {
    return SQL_INVALID_HANDLE;
  }
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  connection->setError(ErrorInfo());
  WriteLog(LL_TRACE,
           "  Application is requesting connection attribute: " +
               std::to_string(Attribute));
  switch (Attribute) {
    case (SQL_ATTR_CONNECTION_DEAD): {
      // A SQLUINTEGER, not a string. The driver keeps no persistent socket,
      // so the connection is never known to be dead.
      if (Value) {
        *reinterpret_cast<SQLUINTEGER*>(Value) = SQL_CD_FALSE;
      }
      if (StringLengthPtr) {
        *StringLengthPtr = sizeof(SQLUINTEGER);
      }
      break;
    }
    case (SQL_ATTR_AUTOCOMMIT):
    case (SQL_ATTR_LOGIN_TIMEOUT): {
      // SQLUINTEGERs the application set with SQLSetConnectAttr.
      if (Value) {
        *reinterpret_cast<SQLUINTEGER*>(Value) =
            Attribute == SQL_ATTR_AUTOCOMMIT
                ? static_cast<SQLUINTEGER>(connection->ATTR_AutoCommitMode)
                : connection->ATTR_LoginTimeout;
      }
      if (StringLengthPtr) {
        *StringLengthPtr = sizeof(SQLUINTEGER);
      }
      break;
    }
    case (SQL_ATTR_CURRENT_CATALOG): {
      // The session's catalog (DSN catalog, SQLSetConnectAttr or USE);
      // "system" when none is set, as before.
      std::string catalog = connection->getCurrentCatalog();
      if (catalog.empty()) {
        catalog = "system";
      }
      if (writeNullTermStringToPtr(
              Value, catalog, BufferLength, StringLengthPtr)) {
        connection->setError(
            ErrorInfo("String data, right truncated", "01004"));
        return SQL_SUCCESS_WITH_INFO;
      }
      break;
    }
    default: {
      WriteLog(LL_ERROR,
               "  ERROR: Application is requesting unimplemented connection "
               "attribute: " +
                   std::to_string(Attribute));
      connection->setError(
          ErrorInfo("Invalid attribute/option identifier", "HY092"));
      return SQL_ERROR;
    }
  }
  return SQL_SUCCESS;
}
