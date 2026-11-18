# Universal (arm64 + x86_64) static dependencies for the macOS driver.
# Excel for Mac is a universal app, so the driver dylib must contain both
# architectures, and static deps keep it free of /opt/homebrew references
# (Excel's sandbox can't read them).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "arm64;x86_64")
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.3)
