# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- `tokenLifetime` option: how long an OAuth token whose expiry can't be read
  is used before signing in again (default: until the server rejects it).

### Fixed

- A token rejected by Trino (HTTP 401, e.g. expired or revoked) is renewed
  and the request repeated once, also while reading results, instead of
  failing until the application reconnects. With OAuth 2.0 refresh tokens
  on the server the renewal needs no browser sign-in.
- External Authentication failed right after signing in when Trino issued
  encrypted tokens (OAuth 2.0 refresh tokens enabled on the server).
- A `varchar` or `decimal` column described without type arguments crashed
  the driver.

## [0.2.0] - 2026-11-19

### Added

- Password authentication (`authmethod = Password`): HTTP Basic with `user`
  and `password`, or the `UID` and `PWD` an application such as Excel passes
  from its login prompt (also through `SQLConnect`).
- Access token authentication (`authmethod = Access Token`): a bearer token
  from `accessToken`, or from the password field.
- `user` option: the Trino user, also for `No Auth`. The Windows setup
  dialog has a User field.
- Connection string values in braces (`PWD={a;b}`, `}}` for `}`), so
  passwords may contain `;`.

### Changed

- `No Auth` connections use the login name of the current user as the Trino
  user (like the Trino CLI) instead of the fixed `TestUser`.
- Errors carry specific SQLSTATEs: 28000 for missing or rejected credentials
  (HTTP 401), 08S01 when the server can't be reached. They were all HY000.
- An unknown `authmethod` is reported with the list of valid names.

### Fixed

- Data source values longer than 2 KB (such as access tokens) were cut off.
- Clearing Catalog or Schema in the Windows setup dialog kept the old value.
- The Windows setup dialog no longer logs secrets passed to `ConfigDSN`.

## [0.1.0] - 2026-11-19

First release.

### Added

- macOS support: driver for Excel for Mac through iODBC, universal (arm64 +
  x86_64) statically linked build, Keychain-protected token cache.
- Releases include the macOS installer next to the Windows MSIs.
- macOS installer (`.pkg`): the driver plus, when missing, OpenLink's iODBC
  frameworks and iODBC Administrator; system DSNs can be created without
  `sudo`; `uninstall.sh`.
- ODBC Unicode (W) API on macOS.
- `ansiEncoding` option: Excel for Mac's narrow text (data, SQL text, names,
  messages, parameters) in the Mac encoding of its UI language; `auto`
  detects it.
- TLS options `sslVerification` (`FULL`, `CA`, `NONE`) and `sslCaFile`.
- `catalog` and `schema` options (also in the Windows setup dialog): the
  session's default catalog and schema for unqualified table names. `USE`
  and `SQL_ATTR_CURRENT_CATALOG` change them.
- macOS: TLS trusts what the Keychain trusts for TLS (system roots and roots
  added by the user, an administrator or an MDM profile), so corporate
  certificate authorities work without `sslCaFile`.
- Docker test environment with Trino and Keycloak; unit and integration
  tests, including end-to-end tests through the iODBC driver manager.
- CI for macOS and Windows with strict warnings, sanitizers, clang-tidy and
  clang-format checks.
- Windows CI runs the integration tests against recorded Trino responses
  (`test/replay`).

### Changed

- Windows: TLS through Schannel with best-effort revocation checks; long text
  and Unicode handling in `SQLGetData` (chunked reads, `SQL_C_WCHAR`).
- Configurable log file location (`TRINO_ODBC_LOG_FILE`).

### Fixed

- Token refresh loops, crashes on connect and in the OAuth device flow,
  secrets in logs, curl header leaks.
- Statement lifecycle, diagnostics, data conversions and buffer overflows.
- System DSNs in `/Library/ODBC/odbc.ini` were ignored on macOS.
- Errors when External Auth can't reach Trino now report the real cause.
- `SQLSetConnectOption` (ODBC 2), which Excel for Mac calls for the login
  timeout, failed for every option; `SQLGetConnectAttr` can now read the
  login timeout and autocommit mode.
- Date, time and timestamp columns convert to each other as ODBC specifies
  (Excel for Mac fetches DATE columns as timestamps).

[Unreleased]: https://github.com/FreakingMind/odbc-for-trino/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/FreakingMind/odbc-for-trino/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/FreakingMind/odbc-for-trino/releases/tag/v0.1.0
