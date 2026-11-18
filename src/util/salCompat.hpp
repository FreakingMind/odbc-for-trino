#pragma once

/*
The ODBC entry points carry Microsoft SAL annotations (_In_, _Out_opt_, ...)
copied from the Windows SDK headers. They only matter to MSVC's static
analyzer; define them away on other compilers.
*/
#ifndef _WIN32

#define _In_
#define _In_reads_(size)
#define _In_reads_opt_(size)
#define _In_reads_bytes_opt_(size)
#define _Inout_opt_
#define _Inout_updates_opt_(size)
#define _Inout_updates_bytes_opt_(size)
#define _Out_
#define _Out_opt_
#define _Out_writes_(size)
#define _Out_writes_opt_(size)
#define _Out_writes_bytes_opt_(size)
#define _Success_(expr)
#define _Inexpressible_(expr)

#endif
