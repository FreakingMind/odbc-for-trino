# Runtime sanitizers for debug and CI builds.
#
#   trino_odbc_enable_sanitizers(<target>)
#
# Options:
#   TRINO_ODBC_SANITIZERS  Semicolon-separated list, e.g. "address;undefined".
#                          LeakSanitizer is part of "address" on Linux; on
#                          macOS use "address" and run with
#                          ASAN_OPTIONS=detect_leaks=1 where supported, or
#                          the `leaks` tool (see README).
#                          MSVC supports only "address".

set(TRINO_ODBC_SANITIZERS "" CACHE STRING
    "Sanitizers to enable (address;undefined;thread)")

function(trino_odbc_enable_sanitizers target)
  if (NOT TRINO_ODBC_SANITIZERS)
    return()
  endif()

  if (MSVC)
    if (NOT TRINO_ODBC_SANITIZERS STREQUAL "address")
      message(FATAL_ERROR "MSVC only supports TRINO_ODBC_SANITIZERS=address")
    endif()
    target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
    return()
  endif()

  list(JOIN TRINO_ODBC_SANITIZERS "," sanitizer_list)
  set(flags -fsanitize=${sanitizer_list} -fno-omit-frame-pointer)
  if ("undefined" IN_LIST TRINO_ODBC_SANITIZERS)
    # Make UBSan findings fatal so tests fail instead of just logging.
    list(APPEND flags -fno-sanitize-recover=all)
  endif()
  target_compile_options(${target} PRIVATE ${flags})
  target_link_options(${target} PRIVATE -fsanitize=${sanitizer_list})
endfunction()
