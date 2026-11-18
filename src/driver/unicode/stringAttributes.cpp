#include "stringAttributes.hpp"

bool isStringInfoType(SQLUSMALLINT infoType) {
  // The character-string info types from the SQLGetInfo reference.
  switch (infoType) {
    case SQL_ACCESSIBLE_PROCEDURES:
    case SQL_ACCESSIBLE_TABLES:
    case SQL_CATALOG_NAME:
    case SQL_CATALOG_NAME_SEPARATOR:
    case SQL_CATALOG_TERM:
    case SQL_COLLATION_SEQ:
    case SQL_COLUMN_ALIAS:
    case SQL_DATA_SOURCE_NAME:
    case SQL_DATA_SOURCE_READ_ONLY:
    case SQL_DATABASE_NAME:
    case SQL_DBMS_NAME:
    case SQL_DBMS_VER:
    case SQL_DESCRIBE_PARAMETER:
    case SQL_DM_VER:
    case SQL_DRIVER_NAME:
    case SQL_DRIVER_ODBC_VER:
    case SQL_DRIVER_VER:
    case SQL_EXPRESSIONS_IN_ORDERBY:
    case SQL_IDENTIFIER_QUOTE_CHAR:
    case SQL_INTEGRITY:
    case SQL_KEYWORDS:
    case SQL_LIKE_ESCAPE_CLAUSE:
    case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
    case SQL_MULT_RESULT_SETS:
    case SQL_MULTIPLE_ACTIVE_TXN:
    case SQL_NEED_LONG_DATA_LEN:
    case SQL_ODBC_VER:
    case SQL_ORDER_BY_COLUMNS_IN_SELECT:
    case SQL_OUTER_JOINS:
    case SQL_PROCEDURE_TERM:
    case SQL_PROCEDURES:
    case SQL_ROW_UPDATES:
    case SQL_SCHEMA_TERM:
    case SQL_SEARCH_PATTERN_ESCAPE:
    case SQL_SERVER_NAME:
    case SQL_SPECIAL_CHARACTERS:
    case SQL_TABLE_TERM:
    case SQL_USER_NAME:
    case SQL_XOPEN_CLI_YEAR:
      return true;
    default:
      return false;
  }
}

bool isStringConnectAttribute(SQLINTEGER attribute) {
  // SQL_ATTR_CURRENT_CATALOG == SQL_CURRENT_QUALIFIER,
  // SQL_ATTR_TRACEFILE == SQL_OPT_TRACEFILE,
  // SQL_ATTR_TRANSLATE_LIB == SQL_TRANSLATE_DLL.
  switch (attribute) {
    case SQL_ATTR_CURRENT_CATALOG:
    case SQL_ATTR_TRACEFILE:
    case SQL_ATTR_TRANSLATE_LIB:
      return true;
    default:
      return false;
  }
}

bool isStringDescriptorField(SQLSMALLINT field) {
  switch (field) {
    case SQL_COLUMN_NAME: // ODBC 2 SQLColAttributes
    case SQL_DESC_BASE_COLUMN_NAME:
    case SQL_DESC_BASE_TABLE_NAME:
    case SQL_DESC_CATALOG_NAME:
    case SQL_DESC_LABEL:
    case SQL_DESC_LITERAL_PREFIX:
    case SQL_DESC_LITERAL_SUFFIX:
    case SQL_DESC_LOCAL_TYPE_NAME:
    case SQL_DESC_NAME:
    case SQL_DESC_SCHEMA_NAME:
    case SQL_DESC_TABLE_NAME:
    case SQL_DESC_TYPE_NAME:
      return true;
    default:
      return false;
  }
}
