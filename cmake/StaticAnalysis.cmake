# clang-tidy integration. Checks are configured in the repository's
# .clang-tidy (including misc-include-cleaner for include hygiene).
#
#   trino_odbc_enable_clang_tidy(<target>)
#
# Options:
#   TRINO_ODBC_CLANG_TIDY  Run clang-tidy while compiling.

option(TRINO_ODBC_CLANG_TIDY "Run clang-tidy during the build" OFF)

if (TRINO_ODBC_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE
               NAMES clang-tidy
               HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
               REQUIRED)
  message(STATUS "clang-tidy: ${CLANG_TIDY_EXE}")
endif()

function(trino_odbc_enable_clang_tidy target)
  if (NOT TRINO_ODBC_CLANG_TIDY)
    return()
  endif()
  set(tidy_command ${CLANG_TIDY_EXE})
  if (APPLE)
    # Homebrew's clang-tidy doesn't know Apple's SDK location by itself.
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE sdk_path
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    list(APPEND tidy_command "--extra-arg=-isysroot${sdk_path}")
  endif()
  set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${tidy_command}")
endfunction()
