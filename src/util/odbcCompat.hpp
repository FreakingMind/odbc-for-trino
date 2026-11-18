#pragma once

#include <sql.h>
#include <sqlext.h>

/*
iODBC's headers stop at ODBC 3.51. Define the ODBC 3.8 constants the driver
reports, with the values from the ODBC specification / Windows SDK.
*/
#ifndef SQL_API_SQLCANCELHANDLE
#define SQL_API_SQLCANCELHANDLE 1550
#endif

#ifndef SQL_CVT_GUID
#define SQL_CVT_GUID 0x01000000L
#endif

#ifndef SQL_ASYNC_DBC_FUNCTIONS
#define SQL_ASYNC_DBC_FUNCTIONS 10023
#endif

#ifndef SQL_ASYNC_DBC_NOT_CAPABLE
#define SQL_ASYNC_DBC_NOT_CAPABLE 0x00000000L
#endif

#ifndef SQL_ASYNC_NOTIFICATION
#define SQL_ASYNC_NOTIFICATION 10025
#endif

#ifndef SQL_ASYNC_NOTIFICATION_NOT_CAPABLE
#define SQL_ASYNC_NOTIFICATION_NOT_CAPABLE 0x00000000L
#endif
