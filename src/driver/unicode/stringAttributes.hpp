#pragma once

#include "../../util/windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

/*
Which ODBC info types, attributes and descriptor fields have string values.
Only those are converted by the W functions; everything else (integers,
pointers, handles) is passed through to the ANSI implementation unchanged.
*/

// SQLGetInfo info types that return a character string.
bool isStringInfoType(SQLUSMALLINT infoType);

// Connection attributes (SQLGet/SetConnectAttr) and ODBC 2 connection
// options (SQLSetConnectOption) whose value is a character string.
bool isStringConnectAttribute(SQLINTEGER attribute);

// Descriptor fields (SQLGet/SetDescField) and column attributes
// (SQLColAttribute) whose value is a character string.
bool isStringDescriptorField(SQLSMALLINT field);
