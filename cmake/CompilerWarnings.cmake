# Strict, per-target compiler warnings.
#
#   trino_odbc_set_warnings(<target>)
#
# Options:
#   TRINO_ODBC_WARNINGS_AS_ERRORS  Turn warnings into errors (CI).

option(TRINO_ODBC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(trino_odbc_set_warnings target)
  if (MSVC)
    set(warnings /W4)
    if (TRINO_ODBC_WARNINGS_AS_ERRORS)
      list(APPEND warnings /WX)
    endif()
  else()
    set(warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wcast-align
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wmisleading-indentation
    )
    if (TRINO_ODBC_WARNINGS_AS_ERRORS)
      list(APPEND warnings -Werror)
    endif()
  endif()
  target_compile_options(${target} PRIVATE ${warnings})
endfunction()
