# Find iODBC: headers and the static ODBC installer library (libiodbcinst).
#
# Excel for Mac uses iODBC, so the macOS driver is built against its headers
# (SQLWCHAR == wchar_t, 4 bytes).
#
# Hints: IODBC_ROOT (install prefix, e.g. `brew --prefix libiodbc`).
#
# Result variables:
#   IODBC_FOUND, IODBC_INCLUDE_DIR, IODBCINST_STATIC_LIBRARY

# IODBC_ROOT wins; then CMAKE_PREFIX_PATH (e.g. vcpkg's installed tree) via
# the default search; Homebrew locations are only a fallback.
set(_iodbc_hints ${IODBC_ROOT} $ENV{IODBC_ROOT})
set(_iodbc_fallback_paths
    /opt/homebrew/opt/libiodbc
    /usr/local/opt/libiodbc)

find_path(IODBC_INCLUDE_DIR NAMES isql.h
          HINTS ${_iodbc_hints}
          PATHS ${_iodbc_fallback_paths}
          PATH_SUFFIXES include)
find_library(IODBCINST_STATIC_LIBRARY NAMES libiodbcinst.a
             HINTS ${_iodbc_hints}
             PATHS ${_iodbc_fallback_paths}
             PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(IODBC
  REQUIRED_VARS IODBC_INCLUDE_DIR IODBCINST_STATIC_LIBRARY)

mark_as_advanced(IODBC_INCLUDE_DIR IODBCINST_STATIC_LIBRARY)
