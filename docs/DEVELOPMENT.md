# Development guide

This guide covers building the driver from source, running the tests and the
layout of the repository. For installing and using the driver, see the
[README](../README.md).

## Contents

- [Requirements](#requirements)
- [Building on macOS](#building-on-macos)
- [Building on Windows](#building-on-windows)
- [Testing](#testing)
- [Code quality](#code-quality)
- [Project layout](#project-layout)

## Requirements

- CMake 3.21 or later and a C++20 compiler (Apple Clang 15 or later, MSVC 2022)
- [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
  for release builds (`VCPKG_ROOT` pointing at a checkout)
- Docker, for the integration tests

## Building on macOS

### Development build

Debug builds link the dependencies from Homebrew:

```sh
brew install cmake ninja libiodbc googletest nlohmann-json howard-hinnant-date
cmake --preset macos-debug
cmake --build --preset macos-debug
```

| Preset | Purpose |
|---|---|
| `macos-debug` | Development build with tests |
| `macos-release` | Optimized build for the host architecture |
| `macos-asan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `macos-tidy` | Build with clang-tidy |
| `macos-universal` | Release build: universal (arm64 + x86_64), statically linked through vcpkg |

### Release build and installer

The release build depends only on macOS system libraries, which Excel's
sandbox requires. Dependencies are built by vcpkg with the overlays in
`vcpkg-overlays/` (iODBC has no vcpkg port of its own):

```sh
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-universal
cmake --build --preset macos-universal
scripts/macos/build-pkg.sh --version 1.2.3
```

`build-pkg.sh` produces `out/pkg/TrinoODBC-<version>.pkg`. The package
contains the driver and the iODBC frameworks and iODBC Administrator, taken
unchanged from OpenLink's signed iODBC SDK, which the script downloads and
checks against a pinned SHA-256. Pass `--sign "Developer ID Installer: ..."`
to sign the package.

CI builds both on every push (artifacts `libTrinoODBC-macos-universal` and
`TrinoODBC-macos-pkg`); release tags attach the package to the release.

### Installing a development build

To try a build in Excel without making a package:

```sh
sudo scripts/macos/install-dev-driver.sh [--lib PATH] [--dsn-file FILE] [--ca-bundle FILE]
```

It installs the driver into `/Library/ODBC/TrinoODBC`, registers it and adds a
`TrinoLocal` data source for the Docker test environment, plus any data
sources from an ini fragment.

## Building on Windows

Install Visual Studio 2022 with the C++ workload and vcpkg, set `VCPKG_ROOT`,
then build one of the presets `x64-debug`, `x64-release`, `x86-debug` or
`x86-release`:

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
```

The MSI installer is built with [WiX](https://wixtoolset.org/) 5:

```powershell
dotnet tool install --global wix --version 5.0.2
wix extension add -g WixToolset.UI.wixext@5.0.2
cd install
./build_x64_installer.ps1   # or ./build_x86_installer.ps1
```

To use a development build without the MSI, register it with the keys in
`install/RegisterTrinoODBC.reg`, pointing `Driver` and `Setup` at the built
`TrinoODBC.dll`.

## Testing

### Unit tests

Unit tests need no server:

```sh
ctest --preset macos-unit          # or macos-asan-unit, x64-debug-unit, ...
```

### Integration tests

Integration tests run against Trino in Docker. `docker/compose.yml` starts a
server without authentication, a server with password and OAuth 2.0
authentication over TLS, and Keycloak as the identity provider. All
credentials are test values from `docker/test.env`.

```sh
docker/setup.sh                              # once: TLS certificate, configs, DSNs
docker compose -f docker/compose.yml up -d
export ODBCINI=$PWD/docker/.generated/odbc.ini
export ODBCINSTINI=$PWD/docker/.generated/odbcinst.ini
ctest --test-dir out/build/macos-debug -L integration
```

Besides the driver test suite this runs two clients through the real iODBC
driver manager, the way Excel for Mac loads the driver: `iodbc_unicode` (a
Unicode application) and `iodbc_ansi` (an ANSI application using Mac
Cyrillic).

On Windows the test executable connects to the data sources `TrinoTestDebug`
or `TrinoTestRelease`, depending on the build type; create them for the
matching driver build.

### Recorded Trino responses

Trino doesn't run on Windows, and GitHub's Windows runners can't run Linux
containers, so Windows CI runs the integration tests against recorded Trino
responses. `test/replay/trino_replay.py` records the HTTP conversation of the
`replayable` tests (the integration tests except performance, memory and
authentication tests) with a real Trino and replays it as a stand-in server.

Re-record after changing the integration tests, and commit the result:

```sh
test/replay/record.sh            # needs the Docker environment and a macos-debug build
```

To run the tests against the recordings locally, start
`python3 test/replay/trino_replay.py replay --recordings test/replay/recordings.json.gz --port 18080`
and point the test data source at `http://localhost:18080`.

### Debugging

Set `loglevel=Trace` in the data source and read the driver log (locations
are listed in the README). Excel for Mac's log is inside its sandbox
container.

## Code quality

| Workflow | Runs on | Checks |
|---|---|---|
| macOS CI | Pushes to branches, pull requests | Debug and ASan + UBSan builds with unit tests, a leak check, clang-format, the universal release build and the installer; clang-tidy on the files a pull request changes |
| Windows CI | Pushes to branches, pull requests | x64 Debug build with unit tests, and integration tests against recorded Trino responses |
| Pull Request Code Scan | Pull requests changing C++ files | Trailing whitespace, final newlines, clang-format |
| Make a Release | Tags `vX.Y.Z` | Windows and macOS installers, draft release |

Builds use the strict warning set from `cmake/CompilerWarnings.cmake`.

Run `clang-format -i` on changed files before committing.

## Project layout

| Path | Contents |
|---|---|
| `src/driver` | ODBC entry points, handles, configuration |
| `src/driver/unicode` | ODBC Unicode (W) entry points |
| `src/trinoAPIWrapper` | Trino REST client, authentication, TLS |
| `src/platform/<os>` | Platform-specific code behind small interfaces |
| `src/util` | Conversions, logging and other helpers |
| `test` | Unit, integration and driver-manager tests |
| `test/replay` | Recorded Trino responses for Windows CI |
| `docker` | Test environment |
| `install` | Windows installer (WiX) |
| `scripts/macos` | macOS installer and development scripts |
| `vcpkg-overlays` | vcpkg triplet and ports for the universal macOS build |
