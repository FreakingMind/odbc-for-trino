#include "../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstddef>
#include <string>

#include "../util/writeLog.hpp"
#include "handles/connHandle.hpp"

SQLRETURN SQL_API SQLSetConnectOption(SQLHDBC ConnectionHandle,
                                      SQLUSMALLINT Option,
                                      SQLULEN Value) {
  // The ODBC 2 version (Excel for Mac uses it for SQL_LOGIN_TIMEOUT). Its
  // options are the ODBC 3 attributes; string values are NUL-terminated.
  WriteLog(LL_TRACE,
           "Entering SQLSetConnectOption: option " + std::to_string(Option));
  if (Option == SQL_CURRENT_QUALIFIER) {
    return SQLSetConnectAttr(ConnectionHandle,
                             SQL_ATTR_CURRENT_CATALOG,
                             reinterpret_cast<SQLPOINTER>(Value),
                             SQL_NTS);
  }
  return SQLSetConnectAttr(
      ConnectionHandle, Option, reinterpret_cast<SQLPOINTER>(Value), 0);
}

SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC ConnectionHandle,
                                    SQLINTEGER Attribute,
                                    _In_reads_bytes_opt_(StringLength)
                                        SQLPOINTER Value,
                                    SQLINTEGER StringLength) {
  /*
  The Value parameter claims to be a SQLPOINTER, but it is only a pointer
  in the event that the attribute that is being set is a string type attribute.
  When setting integer type attributes such as SQL_ATTR_AUTOCOMMIT, SQLPOINTER
  is not a pointer, it's a 32 bit integer that is passed by value.
  */

  WriteLog(LL_TRACE, "Entering SQLSetConnectAttr");
  Connection* connection = reinterpret_cast<Connection*>(ConnectionHandle);
  WriteLog(LL_TRACE,
           "  Request to set attribute: " + std::to_string(Attribute));

  if (connection == nullptr) {
    WriteLog(LL_ERROR, "  ERROR: ConnectionHandle is invalid.");
    return SQL_ERROR;
  }

  switch (Attribute) {
    case SQL_ATTR_AUTOCOMMIT: { // 102
      SQLINTEGER autocommitMode =
          static_cast<SQLINTEGER>(reinterpret_cast<std::uintptr_t>(Value));
      connection->ATTR_AutoCommitMode = autocommitMode;
      WriteLog(LL_TRACE,
               "  Autocommit mode set to: " + std::to_string(autocommitMode));
      break;
    }
    case SQL_ATTR_CURRENT_CATALOG: { // 109
      if (Value == nullptr) {
        connection->setError(ErrorInfo("Invalid use of null pointer", "HY009"));
        return SQL_ERROR;
      }
      const std::string catalog =
          StringLength == SQL_NTS
              ? std::string(static_cast<const char*>(Value))
              : std::string(static_cast<const char*>(Value),
                            static_cast<std::size_t>(
                                StringLength > 0 ? StringLength : 0));
      connection->setCurrentCatalog(catalog);
      WriteLog(LL_TRACE, "  Current catalog set to: " + catalog);
      break;
    }
    case SQL_ATTR_LOGIN_TIMEOUT: { // 103
      SQLUINTEGER loginTimeout =
          static_cast<SQLUINTEGER>(reinterpret_cast<std::uintptr_t>(Value));
      connection->ATTR_LoginTimeout = loginTimeout;
      WriteLog(LL_TRACE,
               "  Login timeout set to: " + std::to_string(loginTimeout));
      break;
    }
    default: {
      WriteLog(LL_ERROR, "  ERROR: Unsupported attribute in SetConnectAttr.");
      WriteLog(LL_ERROR, "  Attribute is " + std::to_string(Attribute));
      return SQL_ERROR;
    }
  }
  WriteLog(LL_TRACE, "  Successful connect attribute setting.");
  return SQL_SUCCESS;
}
