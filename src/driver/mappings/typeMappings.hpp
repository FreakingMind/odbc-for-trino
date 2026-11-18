#pragma once

#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <string>
#include <unordered_map>

#include "../../trinoAPIWrapper/columnDescription.hpp"

extern std::unordered_map<std::string, SQLSMALLINT>
    TRINO_RAW_TYPE_TO_ODBC_TYPE_CODE;

extern std::unordered_map<std::string, SQLLEN>
    TRINO_RAW_TYPE_TO_ODBC_SIZE_BYTES;

extern std::unordered_map<std::string, bool> TRINO_RAW_TYPE_TO_UNSIGNED;

extern std::unordered_map<std::string, SQLSMALLINT>
    TRINO_RAW_TYPE_TO_NUM_PREC_RADIX;

extern std::unordered_map<std::string, SQLCHAR> TRINO_RAW_TYPE_TO_PRECISION;

// Excel worksheet cells can store at most 32,767 characters.  Advertising
// this practical maximum for Trino's unbounded varchar avoids the client's
// 100-character fallback while keeping buffer sizes finite.
constexpr SQLULEN TRINO_ODBC_MAX_TEXT_CHARS = 32767;

SQLULEN inferODBCColumnSize(const ColumnDescription& description);
SQLLEN inferODBCOctetLength(const ColumnDescription& description);
SQLSMALLINT inferODBCTypeCode(const ColumnDescription& description);
