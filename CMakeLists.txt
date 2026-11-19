# CMakeList.txt : CMake project for TrinoODBC, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.21)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("TrinoODBC" VERSION 0.2.0)

# The driver version, reported as SQL_DRIVER_VER (##.##.####).
function(trino_odbc_zero_pad value width out)
  string(LENGTH "${value}" length)
  math(EXPR missing "${width} - ${length}")
  if (missing GREATER 0)
    string(REPEAT "0" ${missing} zeros)
    set(value "${zeros}${value}")
  endif()
  set(${out} "${value}" PARENT_SCOPE)
endfunction()
trino_odbc_zero_pad("${PROJECT_VERSION_MAJOR}" 2 _ver_major)
trino_odbc_zero_pad("${PROJECT_VERSION_MINOR}" 2 _ver_minor)
trino_odbc_zero_pad("${PROJECT_VERSION_PATCH}" 4 _ver_patch)
set(TRINO_ODBC_DRIVER_VER "${_ver_major}.${_ver_minor}.${_ver_patch}")
configure_file(src/version.hpp.in "${CMAKE_BINARY_DIR}/generated/version.hpp" @ONLY)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(StaticAnalysis)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# Define the DEBUG definition so it can be used in preprocessor
# directives to do conditional compilation.
if (CMAKE_BUILD_TYPE MATCHES Debug)
  add_definitions(-DDEBUG)
endif()


find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Add source to this project's library
set(TRINO_ODBC_COMMON_SOURCES
            "src/trinoAPIWrapper/authProvider/tokens/tokenCache.cpp"
            "src/trinoAPIWrapper/authProvider/tokens/tokenParser.cpp"
            "src/trinoAPIWrapper/authProvider/clientCredAuthProvider.cpp"
            "src/trinoAPIWrapper/authProvider/externalAuthProvider.cpp"
            "src/trinoAPIWrapper/authProvider/noAuthProvider.cpp"
            "src/trinoAPIWrapper/authProvider/credentialsAuthProvider.cpp"
            "src/trinoAPIWrapper/authProvider/tokenCacheAuthProviderBase.cpp"
            "src/trinoAPIWrapper/authProvider/deviceFlowAuthProvider.cpp"
            "src/trinoAPIWrapper/trinoQuery.cpp"
            "src/trinoAPIWrapper/connectionConfig.cpp"
            "src/trinoAPIWrapper/environmentConfig.cpp"
            "src/trinoAPIWrapper/columnDescription.cpp"
            "src/trinoAPIWrapper/tlsOptions.cpp"
            "src/trinoAPIWrapper/trinoExceptions.cpp"
            "src/trinoAPIWrapper/trinoOdbcErrorHandler.cpp"
            "src/driver/config/driverConfig.cpp"
            "src/driver/config/profileReader.cpp"
            "src/driver/handles/envHandle.cpp"
            "src/driver/handles/connHandle.cpp"
            "src/driver/handles/statementHandle.cpp"
            "src/driver/handles/descriptorHandle.cpp"
            "src/driver/handles/handleErrorInfo.cpp"
            "src/driver/mappings/typeMappings.cpp"
            "src/util/base64.cpp"
            "src/util/b64decoder.cpp"
            "src/util/capitalize.cpp"
            "src/util/dateAndTimeUtils.cpp"
            "src/util/decimalHelper.cpp"
            "src/util/jsonValueToText.cpp"
            "src/util/delimKvpHelper.cpp"
            "src/util/randomStr.cpp"
            "src/util/redactSecrets.cpp"
            "src/util/rowToBuffer.cpp"
            "src/util/stringFromChar.cpp"
            "src/util/stringReplace.cpp"
            "src/util/stringSplitAndTrim.cpp"
            "src/util/stringTrim.cpp"
            "src/util/timer.cpp"
            "src/util/timezone.cpp"
            "src/util/timeUtils.cpp"
            "src/util/writeLog.cpp"
            "src/driver/allocHandle.cpp"
            "src/driver/bindCol.cpp"
            "src/driver/bindParameter.cpp"
            "src/driver/cancel.cpp"
            "src/driver/cancelHandle.cpp"
            "src/driver/closeCursor.cpp"
            "src/driver/colAttribute.cpp"
            "src/driver/columns.cpp"
            "src/driver/connect.cpp"
            "src/driver/copyDesc.cpp"
            "src/driver/dataSources.cpp"
            "src/driver/describeCol.cpp"
            "src/driver/disconnect.cpp"
            "src/driver/driverConnect.cpp"
            "src/driver/drivers.cpp"
            "src/driver/endTran.cpp"
            "src/driver/execDirect.cpp"
            "src/driver/execute.cpp"
            "src/driver/extendedFetch.cpp"
            "src/driver/fetch.cpp"
            "src/driver/fetchScroll.cpp"
            "src/driver/freeHandle.cpp"
            "src/driver/freeStmt.cpp"
            "src/driver/getConnectAttr.cpp"
            "src/driver/getCursorName.cpp"
            "src/driver/getData.cpp"
            "src/driver/getDescField.cpp"
            "src/driver/getDescRec.cpp"
            "src/driver/getDiagRec.cpp"
            "src/driver/getEnvAttr.cpp"
            "src/driver/getFunctions.cpp"
            "src/driver/getInfo.cpp"
            "src/driver/getStmtAttr.cpp"
            "src/driver/getTypeInfo.cpp"
            "src/driver/moreResults.cpp"
            "src/driver/nativeSql.cpp"
            "src/driver/numParams.cpp"
            "src/driver/numResultCols.cpp"
            "src/driver/paramData.cpp"
            "src/driver/prepare.cpp"
            "src/driver/putData.cpp"
            "src/driver/rowCount.cpp"
            "src/driver/setConnectAttr.cpp"
            "src/driver/setCursorName.cpp"
            "src/driver/setDescField.cpp"
            "src/driver/setDescRec.cpp"
            "src/driver/setEnvAttr.cpp"
            "src/driver/setStmtAttr.cpp"
            "src/driver/statistics.cpp"
            "src/driver/tables.cpp"
)

# Platform-specific code lives in src/platform/<os>/ behind small interfaces.
if (WIN32)
  set(TRINO_ODBC_PLATFORM_SOURCES
      "src/driver/config/configDSN.cpp"
      "src/driver/config/dsnConfigForm.cpp"
      "src/driver/config/win32controls/labelMaker.cpp"
      "src/driver/config/win32controls/comboboxMaker.cpp"
      "src/driver/config/win32controls/editMaker.cpp"
      "src/driver/config/win32controls/buttonMaker.cpp"
      "src/platform/win/browserInteraction.cpp"
      "src/platform/win/cryptUtils.cpp"
      "src/platform/win/platformPaths.cpp"
      "src/platform/win/systemTrustStore.cpp"
      "src/platform/win/textEncoding.cpp"
      "src/platform/win/userName.cpp"
  )
elseif (APPLE)
  set(TRINO_ODBC_PLATFORM_SOURCES
      "src/platform/mac/ansiEncoding.cpp"
      "src/platform/mac/browserInteraction.cpp"
      "src/platform/mac/cryptUtils.cpp"
      "src/platform/mac/platformPaths.cpp"
      "src/platform/mac/systemTrustStore.cpp"
      "src/platform/posix/textEncoding.cpp"
      "src/platform/posix/userName.cpp"
  )
else()
  message(FATAL_ERROR "Unsupported platform: only Windows and macOS are supported")
endif()

# The ODBC Unicode API (SQL*W entry points). Excel for Mac requires it: iODBC
# otherwise converts strings itself in the process locale, which mangles
# non-ASCII text. Off on Windows until it has been validated there.
if (APPLE)
  set(_trino_odbc_unicode_api_default ON)
else()
  set(_trino_odbc_unicode_api_default OFF)
endif()
option(TRINO_ODBC_UNICODE_API "Export the ODBC Unicode (W) functions"
       ${_trino_odbc_unicode_api_default})
if (TRINO_ODBC_UNICODE_API)
  list(APPEND TRINO_ODBC_COMMON_SOURCES
       "src/driver/unicode/connectW.cpp"
       "src/driver/unicode/descriptorW.cpp"
       "src/driver/unicode/diagW.cpp"
       "src/driver/unicode/statementW.cpp"
       "src/driver/unicode/stringAttributes.cpp"
       "src/driver/unicode/unicodeWrappers.cpp"
       "src/driver/unicode/wideText.cpp"
  )
endif()

# All dependency usage requirements live on one interface target shared by
# the driver and the test executable.
add_library(TrinoODBCDeps INTERFACE)
target_link_libraries(TrinoODBCDeps INTERFACE
                      nlohmann_json::nlohmann_json
                      CURL::libcurl)
# Sources and tests are UTF-8; without this MSVC reads them in the ANSI
# code page and mangles non-ASCII literals.
target_compile_options(TrinoODBCDeps INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
if (TRINO_ODBC_UNICODE_API)
  target_compile_definitions(TrinoODBCDeps INTERFACE TRINO_ODBC_UNICODE_API)
endif()

if (WIN32)
  # odbccp32 has linker errors that can be resolved by including the
  # legacy_stdio_defnitions library.
  # https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk2019
  target_link_libraries(TrinoODBCDeps INTERFACE odbccp32 crypt32 legacy_stdio_definitions)
  set(TRINO_ODBC_TEST_DM_LIBS odbc32)
elseif (APPLE)
  # Excel for Mac uses the iODBC driver manager, so build against iODBC
  # headers (SQLWCHAR == wchar_t, 4 bytes).
  #
  # The ODBC installer API (SQLGetPrivateProfileString, ...) is linked
  # statically from libiodbcinst.a: the driver then reads odbc.ini itself and
  # doesn't depend on which driver manager copy the host process (e.g. the
  # sandboxed Excel) has loaded. iODBC is available under the BSD license.
  find_package(IODBC REQUIRED)

  # Apple's libc++ has no std::chrono::tzdb; use Howard Hinnant's date
  # library (the reference implementation of it) with the OS zoneinfo.
  find_package(date CONFIG REQUIRED)

  target_include_directories(TrinoODBCDeps INTERFACE ${IODBC_INCLUDE_DIR})
  target_compile_definitions(TrinoODBCDeps INTERFACE TRINO_ODBC_USE_DATE_TZ)
  target_link_libraries(TrinoODBCDeps INTERFACE
                        date::date-tz
                        "-framework CoreFoundation"
                        "-framework CoreServices"
                        "-framework Security")
  # Tests call the driver's SQL* functions directly; they only need the
  # installer API that the driver itself uses.
  set(TRINO_ODBC_TEST_DM_LIBS ${IODBCINST_STATIC_LIBRARY})
endif()

# The driver code is compiled once into an object library. The ODBC driver
# (shared library) and the test executable both link these objects, so unit
# tests can call internal functions while the driver exports only the ODBC
# API.
add_library(TrinoODBCCore OBJECT
            ${TRINO_ODBC_COMMON_SOURCES}
            ${TRINO_ODBC_PLATFORM_SOURCES}
)
set_target_properties(TrinoODBCCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(TrinoODBCCore PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(TrinoODBCCore PUBLIC TrinoODBCDeps)
trino_odbc_set_warnings(TrinoODBCCore)
trino_odbc_enable_sanitizers(TrinoODBCCore)
trino_odbc_enable_clang_tidy(TrinoODBCCore)

add_library(TrinoODBC SHARED $<TARGET_OBJECTS:TrinoODBCCore>)
target_link_libraries(TrinoODBC PRIVATE TrinoODBCDeps)
trino_odbc_enable_sanitizers(TrinoODBC)

if (APPLE)
  set(TRINO_ODBC_EXPORTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/exports.txt")
  target_link_options(TrinoODBC PRIVATE
                      # Export only the ODBC entry points.
                      "LINKER:-exported_symbols_list,${TRINO_ODBC_EXPORTS_FILE}"
                      # Link the installer API statically but keep it out of
                      # the export table, so it can't clash with the host's DM.
                      "LINKER:-load_hidden,${IODBCINST_STATIC_LIBRARY}")
  set_target_properties(TrinoODBC PROPERTIES
                        LINK_DEPENDS "${TRINO_ODBC_EXPORTS_FILE}"
                        INSTALL_NAME_DIR "@rpath"
                        MACOSX_RPATH ON)
endif()

# We want to be able to write unit tests with gtest
enable_testing()
find_package(GTest CONFIG REQUIRED)
add_executable(TestDriver
    "test/auth/authFlowsTest.cpp"
    "test/connections/connectTest.cpp"
    "test/connections/testCatalogSchema.cpp"
    "test/fixtures/sqlDriverConnectFixture.cpp"
    "test/functions/testAnsiEncoding.cpp"
    "test/functions/testCancel.cpp"
    "test/functions/testColumns.cpp"
    "test/functions/testDescribeCol.cpp"
    "test/functions/testGetConnectAttr.cpp"
    "test/functions/testGetInfo.cpp"
    "test/functions/testStatementLifecycle.cpp"
    "test/functions/testTables.cpp"
    "test/memory/memoryReclamationTest.cpp"
    "test/performance/bindFetchPerformanceTest.cpp"
    "test/performance/fetchRowPerformanceTest.cpp"
    "test/performance/getDataFetchPerformanceTest.cpp"
    "test/preparedStatements/testExecute.cpp"
    "test/preparedStatements/testManualPrepare.cpp"
    "test/preparedStatements/testPrepare.cpp"
    "test/types/fetchBindTest.cpp"
    "test/types/fetchGetDataTest.cpp"
    "test/unit/auth/credentialsAuthTest.cpp"
    "test/unit/auth/tlsOptionsTest.cpp"
    "test/unit/config/driverConfigTest.cpp"
    "test/unit/config/profileReaderTest.cpp"
    "test/unit/auth/tokenExpiryTest.cpp"
    "test/unit/platform/platformPathsTest.cpp"
    "test/unit/platform/systemTrustStoreTest.cpp"
    "test/unit/platform/textEncodingTest.cpp"
    "test/unit/util/base64Test.cpp"
    "test/unit/util/base64decoderTest.cpp"
    "test/unit/util/cryptUtilsTest.cpp"
    "test/unit/util/cryptUtilsMacTest.cpp"
    "test/unit/util/dateAndTimeUtilsTest.cpp"
    "test/unit/util/delimKvpHelperTest.cpp"
    "test/unit/util/randomStrTest.cpp"
    "test/unit/util/rowToBufferTest.cpp"
    "test/unit/util/redactSecretsTest.cpp"
    "test/unit/util/stringTrimTest.cpp"
    "test/unit/util/stringReplaceTest.cpp"
    "test/unit/util/timezoneTest.cpp"
    "test/unit/util/valuePtrHelperTest.cpp"
    "test/constants.cpp"
    "test/gtestTest.cpp"
)
# GMock is broken in VS environment. Use tests only
# https://github.com/google/googletest/issues/2157
target_sources(TestDriver PRIVATE $<TARGET_OBJECTS:TrinoODBCCore>)
target_link_libraries(TestDriver PRIVATE TrinoODBCDeps GTest::gtest_main ${TRINO_ODBC_TEST_DM_LIBS})
trino_odbc_set_warnings(TestDriver)
trino_odbc_enable_sanitizers(TestDriver)

# Unit test suites run without a Trino server; everything else is an
# integration test that needs one (see README).
set(TRINO_ODBC_UNIT_TEST_SUITES
    Base64Test
    Base64urlDecoderTest
    CryptUtilsTest
    CredentialsAuthTest
    CryptUtilsMacTest
    DateAndTimeUtilsTest
    DelimKvpHelperTest
    DescriptorFieldToParameterTextTest
    DriverConfigTest
    GTestTests
    PlatformPathsTest
    ProfileReaderTest
    RandomStrTest
    RowToBufferTest
    RedactSecretsTest
    SqlStateTest
    StringReplaceTest
    StringTrimTest
    SystemTrustStoreTest
    AnsiEncodingTest
    TextEncodingTest
    TimezoneTest
    TlsOptionsTest
    TokenExpiryTest
    ValuePtrHelperTest
)
if (TRINO_ODBC_UNICODE_API)
  target_sources(TestDriver PRIVATE
                 "test/functions/testUnicodeApi.cpp"
                 "test/unit/unicode/wideTextTest.cpp")
  list(APPEND TRINO_ODBC_UNIT_TEST_SUITES
       DriverUnicodeTypeTest
       StringAttributesTest
       UnicodeWrappersOfflineTest
       UnicodeWrappersTest
       WideTextTest)
endif()
list(TRANSFORM TRINO_ODBC_UNIT_TEST_SUITES APPEND ".*" OUTPUT_VARIABLE _unit_filters)
list(JOIN _unit_filters ":" TRINO_ODBC_UNIT_TEST_FILTER)

add_test(NAME unit COMMAND TestDriver --gtest_filter=${TRINO_ODBC_UNIT_TEST_FILTER})
set_tests_properties(unit PROPERTIES LABELS unit)
add_test(NAME integration COMMAND TestDriver --gtest_filter=-${TRINO_ODBC_UNIT_TEST_FILTER})
# Integration tests keep OAuth tokens in memory only: an unsigned, rebuilt
# test binary can't use the keychain without prompting the user.
set_tests_properties(integration PROPERTIES
                     LABELS integration
                     ENVIRONMENT "TRINO_ODBC_TOKEN_CACHE=none")

# The integration tests that can run against recorded Trino responses
# (test/replay): all but performance, memory and authentication tests. CI
# runs them on Windows, where Trino can't run; record them on macOS with
# test/replay/record.sh.
set(_replay_excludes
    "*PerformanceTest.*" "MemoryReclamationTest.*" "AuthFlowsTest.*")
list(JOIN _replay_excludes ":" TRINO_ODBC_REPLAY_EXCLUDES)
add_test(NAME replayable
         COMMAND TestDriver
                 --gtest_filter=-${TRINO_ODBC_UNIT_TEST_FILTER}:${TRINO_ODBC_REPLAY_EXCLUDES})
set_tests_properties(replayable PROPERTIES
                     LABELS replay
                     ENVIRONMENT "TRINO_ODBC_TOKEN_CACHE=none")

# End-to-end check of the Unicode API through the real iODBC driver manager,
# the way Excel for Mac loads the driver (dev/CI tool; needs Homebrew's
# libiodbc and the odbc.ini / odbcinst.ini described in the README).
if (APPLE AND TRINO_ODBC_UNICODE_API)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _trino_odbc_arch_count)
  find_library(IODBC_DM_LIBRARY NAMES libiodbc.dylib iodbc
               HINTS ${IODBC_ROOT} $ENV{IODBC_ROOT}
               PATHS /opt/homebrew/opt/libiodbc /usr/local/opt/libiodbc
               PATH_SUFFIXES lib)
  mark_as_advanced(IODBC_DM_LIBRARY)
  if (IODBC_DM_LIBRARY AND _trino_odbc_arch_count LESS_EQUAL 1)
    add_executable(IodbcUnicodeClient "test/dm/iodbcUnicodeClient.cpp")
    target_include_directories(IodbcUnicodeClient PRIVATE ${IODBC_INCLUDE_DIR})
    target_link_libraries(IodbcUnicodeClient PRIVATE ${IODBC_DM_LIBRARY})
    trino_odbc_set_warnings(IodbcUnicodeClient)
    # The driver must be built first: the DM loads it from odbcinst.ini.
    add_dependencies(IodbcUnicodeClient TrinoODBC)
    add_test(NAME iodbc_unicode COMMAND IodbcUnicodeClient "DSN=TrinoTestDebug")
    set_tests_properties(iodbc_unicode PROPERTIES
                         LABELS integration
                         ENVIRONMENT "TRINO_ODBC_TOKEN_CACHE=none")

    # Smoke test for a real server (run by hand, not by ctest): reads a
    # sample of every table in a schema the way Excel does.
    add_executable(TrinoOdbcSmoke "test/dm/odbcSmoke.cpp")
    target_include_directories(TrinoOdbcSmoke PRIVATE ${IODBC_INCLUDE_DIR})
    target_link_libraries(TrinoOdbcSmoke PRIVATE ${IODBC_DM_LIBRARY})
    trino_odbc_set_warnings(TrinoOdbcSmoke)

    # The same for an ANSI application in a legacy encoding (Excel's
    # Microsoft Query with a Russian UI).
    add_executable(IodbcAnsiClient "test/dm/iodbcAnsiClient.cpp")
    target_include_directories(IodbcAnsiClient PRIVATE ${IODBC_INCLUDE_DIR})
    target_link_libraries(IodbcAnsiClient PRIVATE ${IODBC_DM_LIBRARY})
    trino_odbc_set_warnings(IodbcAnsiClient)
    add_dependencies(IodbcAnsiClient TrinoODBC)
    add_test(NAME iodbc_ansi COMMAND IodbcAnsiClient "DSN=TrinoTestDebug")
    set_tests_properties(iodbc_ansi PROPERTIES
                         LABELS integration
                         ENVIRONMENT "TRINO_ODBC_TOKEN_CACHE=none")
  endif()
endif()
