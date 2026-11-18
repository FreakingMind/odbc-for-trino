#pragma once

#include <nlohmann/json.hpp>
#include <string>

/*
Text form of a Trino JSON value for SQL_C_CHAR / SQL_C_WCHAR output.

ODBC requires every SQL type to be convertible to character data. Trino
sends VARCHAR, DECIMAL, DATE, TIMESTAMP, ... as JSON strings, but numbers,
booleans and structured types (ARRAY, MAP, ROW) as JSON values.

- string:  returned unchanged (UTF-8)
- boolean: "1" / "0" (ODBC's SQL_BIT to SQL_C_CHAR conversion)
- integer: decimal digits
- float:   shortest round-trip representation
- array / object: compact JSON text
*/
std::string jsonValueToText(const nlohmann::json& value);
