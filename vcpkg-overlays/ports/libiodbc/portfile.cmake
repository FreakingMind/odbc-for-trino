# Overlay port: vcpkg has no libiodbc port. Builds the iODBC driver manager
# (libiodbc) and installer API (libiodbcinst) from the official release
# tarball. Universal (arm64 + x86_64) builds are handled by vcpkg's autotools
# support when VCPKG_OSX_ARCHITECTURES lists both architectures.

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/openlink/iODBC/releases/download/v${VERSION}/libiodbc-${VERSION}.tar.gz"
    FILENAME "libiodbc-${VERSION}.tar.gz"
    SHA512 8ccc88ec0dc9f08ef549be7c1a8355aeba0948f144d2b06e97e6005dc5e7f992c4c634a67260eba35799ccfd972e1846dd6ba1df1a9acd8d6151d9939f38c175
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

set(options "")
list(LENGTH VCPKG_OSX_ARCHITECTURES osx_arch_count)
if(osx_arch_count GREATER 1)
    list(APPEND options
        "CPP=/bin/sh ${CMAKE_CURRENT_LIST_DIR}/cpp-single-arch.sh"
        # Compiler-generated dependency files don't work with several -arch.
        --disable-dependency-tracking)
endif()

vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
        --disable-gui
        --disable-libodbc
        # System-wide odbc.ini / odbcinst.ini location baked into
        # libiodbcinst (after $ODBCINI and ~/Library/ODBC). Without this it
        # would point into the vcpkg install tree.
        --with-iodbc-inidir=/Library/ODBC
)

vcpkg_install_make()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/man"
    "${CURRENT_PACKAGES_DIR}/share/libiodbc/man"
)
# Command-line tools (iodbctest, iodbc-config) aren't needed by consumers.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/LICENSE.BSD")
