# ODBC for Trino

[![Release](https://img.shields.io/github/v/release/FreakingMind/odbc-for-trino)](https://github.com/FreakingMind/odbc-for-trino/releases/latest)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![macOS CI](https://github.com/FreakingMind/odbc-for-trino/actions/workflows/macos-ci.yml/badge.svg)](https://github.com/FreakingMind/odbc-for-trino/actions/workflows/macos-ci.yml)
[![Windows CI](https://github.com/FreakingMind/odbc-for-trino/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/FreakingMind/odbc-for-trino/actions/workflows/windows-ci.yml)

An ODBC driver for [Trino](https://trino.io/), built for Microsoft Excel on
Windows and macOS. It lets spreadsheet users browse catalogs, run SQL and load
query results into Excel, and also works with Power BI Desktop on Windows.

## Contents

- [Features](#features)
- [Supported platforms](#supported-platforms)
- [Installation](#installation)
- [Connecting from Excel](#connecting-from-excel)
- [Configuration reference](#configuration-reference)
- [Certificates and TLS](#certificates-and-tls)
- [Non-Latin text in Excel for Mac](#non-latin-text-in-excel-for-mac)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Documentation](#documentation)
- [License](#license)

## Features

- **Excel on both platforms.** Native installers for Windows (MSI) and macOS
  (universal `.pkg` for Apple silicon and Intel).
- **Authentication.** No authentication, External Authentication (browser
  sign-in through Trino), OAuth 2.0 client credentials and device flow.
  Tokens are cached securely: DPAPI on Windows, the Keychain on macOS.
- **TLS that follows the operating system.** Certificates trusted by Windows
  or by the macOS Keychain — including corporate roots deployed by IT — are
  trusted by the driver, with no extra configuration.
- **International text.** Windows applications receive Unicode or the system
  code page, as they request. Excel for Mac receives text in the encoding of
  its interface language (Cyrillic, Central European, Greek, Japanese and
  others).
- **Large results.** Long text values are streamed to the application in
  parts.

## Supported platforms

| Operating system | Architectures | Applications | Driver manager |
|---|---|---|---|
| Windows | x64 | Excel, Power BI Desktop | Windows ODBC |
| Windows | x86 | Excel (32-bit) | Windows ODBC |
| macOS 13.3 or later | Apple silicon, Intel | Excel for Mac (Microsoft Query) | iODBC (installed with the driver) |

## Installation

Download the installer for your platform from the
[releases page](https://github.com/FreakingMind/odbc-for-trino/releases).

### Windows

1. Run `TrinoODBC_x64.msi` (or `TrinoODBC_x86.msi` for 32-bit Excel).
2. Open *ODBC Data Sources (64-bit)* (or 32-bit, matching Excel), choose
   **Add** → **TrinoODBC** and fill in the server details.

### macOS

1. Open `TrinoODBC-<version>.pkg` and follow the installer. Besides the
   driver, it installs the iODBC driver manager that Excel requires and the
   iODBC Administrator app, unless they are already present.

   > The package is not signed yet. If macOS blocks it, open *System
   > Settings → Privacy & Security* and choose **Open Anyway**.

2. Open *Applications → iODBC → iODBC Administrator64*, select the **System
   DSN** tab and choose **Add** → **Trino ODBC**.
3. Enter a data source name (for example `Trino`) and add the connection
   keywords with **+**; double-click a cell to edit it:

   | Keyword | Example |
   |---|---|
   | `hostname` | `https://trino.example.com` |
   | `port` | `443` |
   | `authmethod` | `External Auth` |
   | `catalog` | `iceberg` (optional) |
   | `schema` | `sales` (optional, needs `catalog`) |

   All keywords are listed in the [configuration reference](#configuration-reference).

To uninstall, run `sudo /Library/ODBC/TrinoODBC/uninstall.sh`. Data sources
and iODBC are kept.

## Connecting from Excel

**Excel for Windows:** *Data → Get Data → From Other Sources → From ODBC*,
then choose the data source.

**Excel for Mac:** *Data → Get Data → From Database (Microsoft Query)*, open
the **System DSN** tab and choose the data source. Leave the user name and
password empty: with External Authentication a browser window opens for
sign-in.

Table names are resolved against the data source's `catalog` and `schema`:
with `catalog=iceberg` and `schema=sales`, `orders` means
`iceberg.sales.orders`, and `marketing.campaigns` means
`iceberg.marketing.campaigns`. Without them, use full names. A `USE
catalog.schema` statement changes them for the rest of the session.

## Configuration reference

Data sources are configured in the ODBC administrator of the platform. The
keywords can also be given in a connection string, except
`secretEncryptionLevel`.

### Connection

| Keyword | Default | Description |
|---|---|---|
| `hostname` | `localhost` | Server URL without the port, e.g. `https://trino.example.com` |
| `port` | `8080` | Server port |
| `authmethod` | `No Auth` | `No Auth`, `External Auth`, `OIDC Client Cred Auth` or `Device Flow` |
| `catalog` | | Default catalog for unqualified table names |
| `schema` | | Default schema; requires `catalog` |

### OAuth 2.0

| Keyword | Default | Description |
|---|---|---|
| `oidcDiscoveryUrl` | | OpenID Connect discovery document of the identity provider |
| `clientId` | | Client ID |
| `clientSecret` | | Client secret (client credentials flow) |
| `oidcScope` | | Requested scopes |
| `secretEncryptionLevel` | `user` | Windows: protection scope of the stored client secret |

### TLS

| Keyword | Default | Description |
|---|---|---|
| `sslVerification` | `FULL` | `FULL` (certificate and host name), `CA` (certificate only) or `NONE` |
| `sslCaFile` | | PEM file of trusted CA certificates, used instead of the system trust store |

### Text and diagnostics

| Keyword | Default | Description |
|---|---|---|
| `ansiEncoding` | `auto` | Encoding of text exchanged with ANSI applications: `auto`, `UTF-8`, or on macOS an IANA name such as `x-mac-cyrillic` |
| `loglevel` | `None` | `None`, `Error`, `Warn`, `Info`, `Debug` or `Trace` |

### Environment variables

| Variable | Description |
|---|---|
| `TRINO_ODBC_LOG_FILE` | Log file path (see [Troubleshooting](#troubleshooting) for the defaults) |
| `TRINO_ODBC_TOKEN_CACHE` | `none` keeps OAuth tokens in memory only |
| `TRINO_ODBC_KEYCHAIN_SERVICE` | macOS: Keychain service name of the token cache key |

## Certificates and TLS

By default the driver trusts the certificates the operating system trusts:
the Windows certificate store, or on macOS the system roots and any root
certificate marked as trusted in the Keychain — by the user, an administrator
or an MDM profile. A server whose certificate is issued by a corporate
certificate authority therefore works as soon as that authority is trusted by
the system, as it is for web browsers.

To trust a corporate root certificate on a Mac that doesn't have it yet,
double-click the certificate file, add it to the *login* keychain, then open it
in Keychain Access and set **When using this certificate** to **Always
Trust**.

The driver reads the trust store once per application session: restart
Excel after trusting a new certificate.

`sslCaFile` replaces the system trust store with a specific PEM file. On
macOS the file must be under `/Library/ODBC` (for example
`/Library/ODBC/TrinoODBC/`): Excel is sandboxed and cannot read files in your
home folder.

## Non-Latin text in Excel for Mac

Excel for Mac exchanges text with ODBC drivers in the legacy Mac encoding of
its interface language — Mac Roman for English, Mac Cyrillic for Russian, and
so on — rather than in Unicode. The driver detects this automatically, so
everything that encoding can represent works: data, SQL literals and filters,
column names and error messages.

To work with Cyrillic data, run Excel in Russian. The language of the rest of
the system can stay as it is:

1. Open *System Settings → General → Language & Region*.
2. Under **Applications**, click **+**, choose **Microsoft Excel** and select
   **Русский**.
3. Quit Excel completely (⌘Q) and open it again.

## Troubleshooting

| Symptom | Cause and solution |
|---|---|
| Question marks instead of letters (Excel for Mac) | Excel's interface language has no such letters in its encoding. Set Excel's language as described in [Non-Latin text](#non-latin-text-in-excel-for-mac). |
| Certificate errors | The server certificate isn't trusted by the system. Trust the issuing authority as described in [Certificates and TLS](#certificates-and-tls). |
| iODBC Administrator can't save a system DSN | `/Library/ODBC/odbc.ini` was created by another installer without write access for administrators. Run `sudo chown root:admin /Library/ODBC/odbc.ini && sudo chmod 664 /Library/ODBC/odbc.ini` once. |
| "Table not found" or "Schema must be specified" | Set `catalog` and `schema` in the data source, or use the full name `catalog.schema.table`. |

For anything else, set `loglevel` to `Debug` or `Trace` in the data source and
check the driver log:

| Platform | Log file |
|---|---|
| Windows | `C:\temp\odbclog.txt` |
| macOS, Excel | `~/Library/Containers/com.microsoft.Excel/Data/Library/Logs/TrinoODBC/TrinoODBC.log` |
| macOS, other applications | `~/Library/Logs/TrinoODBC/TrinoODBC.log` |

## Limitations

- Read-only: transactions (`SQLEndTran`) are not supported.
- Rows are fetched one at a time; `SQLFetchScroll`, `SQLExtendedFetch` and
  `SQLBrowseConnect` are not implemented.
- All columns are reported as nullable.
- Password (Basic) authentication is not supported yet.
- Network settings are fixed for now: proxies are bypassed, IPv4 and HTTP/1.1
  are used, and requests time out after 60 seconds.
- macOS has no driver-specific setup dialog; data sources are edited in iODBC
  Administrator.

## Documentation

- [Development guide](docs/DEVELOPMENT.md) — building, testing, project layout
- [Release process](docs/RELEASING.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md) and [Security policy](SECURITY.md)

## License

Licensed under the [Apache License 2.0](LICENSE); see [NOTICE](NOTICE).
Licenses of the third-party libraries shipped with the Windows installer are
listed in [install/third_party_licenses.txt](install/third_party_licenses.txt).

This project is independent and is not affiliated with or endorsed by the
Trino Software Foundation or Microsoft. "Trino" is a trademark of the Trino
Software Foundation.
