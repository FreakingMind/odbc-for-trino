#!/bin/sh
# Generates the files the local test environment needs but that must not be
# committed: a self-signed TLS certificate for trino-secure and the bcrypt
# password file for its PASSWORD authenticator.
set -eu

cd "$(dirname "$0")"
. ./test.env
OUT=.generated
mkdir -p "$OUT"

if [ ! -f "$OUT/server.pem" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -days 825 \
    -keyout "$OUT/server.key" -out "$OUT/server.crt" \
    -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,DNS:trino-secure,IP:127.0.0.1" \
    2>/dev/null
  cat "$OUT/server.key" "$OUT/server.crt" > "$OUT/server.pem"
  # Readable by the trino user inside the container. Test-only key.
  chmod 644 "$OUT/server.pem" "$OUT/server.key"
  echo "Generated $OUT/server.pem (self-signed, CN=localhost)"
fi

if [ ! -f "$OUT/password.db" ]; then
  htpasswd -B -C 10 -b -n "$TRINO_TEST_USER" "$TRINO_TEST_PASSWORD" \
    | sed '/^$/d' > "$OUT/password.db"
  chmod 644 "$OUT/password.db"
  echo "Generated $OUT/password.db (user $TRINO_TEST_USER)"
fi

# ODBC configuration for the test DSNs, used through the ODBCINI /
# ODBCSYSINI-style environment variables so the user's own
# ~/Library/ODBC/*.ini files are never touched:
#
#   export ODBCINI=$PWD/docker/.generated/odbc.ini
#   export ODBCINSTINI=$PWD/docker/.generated/odbcinst.ini
#
# DRIVER_LIB defaults to the macos-debug build output.
DRIVER_LIB="${DRIVER_LIB:-$(cd .. && pwd)/out/build/macos-debug/libTrinoODBC.dylib}"

cat > "$OUT/odbcinst.ini" <<INI
[ODBC Drivers]
Trino ODBC = Installed

[Trino ODBC]
Driver = $DRIVER_LIB
Setup  = $DRIVER_LIB
INI

cat > "$OUT/odbc.ini" <<INI
[ODBC Data Sources]
TrinoTestDebug   = Trino ODBC
TrinoTestRelease = Trino ODBC
TrinoNoAuth      = Trino ODBC
TrinoSecureClientCred   = Trino ODBC
TrinoSecureExternalAuth = Trino ODBC
TrinoSecureDeviceFlow   = Trino ODBC
TrinoSecureNoAuth       = Trino ODBC
TrinoSecureUntrusted    = Trino ODBC
TrinoSecurePassword     = Trino ODBC

; Used by the integration test suite (test/constants.cpp).
[TrinoTestDebug]
Driver     = Trino ODBC
hostname   = http://localhost
port       = 8080
authmethod = No Auth

[TrinoTestRelease]
Driver     = Trino ODBC
hostname   = http://localhost
port       = 8080
authmethod = No Auth

[TrinoNoAuth]
Driver     = Trino ODBC
hostname   = http://localhost
port       = 8080
authmethod = No Auth

; trino-secure over HTTPS, trusting the generated self-signed certificate.
[TrinoSecureClientCred]
Driver           = Trino ODBC
hostname         = https://localhost
port             = 8443
sslCaFile        = $(pwd)/$OUT/server.crt
authmethod       = OIDC Client Cred Auth
oidcDiscoveryUrl = http://localhost:8180/realms/trino/.well-known/openid-configuration
clientId         = $ODBC_CLIENT_CREDENTIALS_ID
clientSecret     = $ODBC_CLIENT_CREDENTIALS_SECRET

; Interactive: opens the browser, log in as $TRINO_TEST_USER / $TRINO_TEST_PASSWORD.
[TrinoSecureExternalAuth]
Driver     = Trino ODBC
hostname   = https://localhost
port       = 8443
sslCaFile  = $(pwd)/$OUT/server.crt
authmethod = External Auth

; Interactive: device code flow against Keycloak.
[TrinoSecureDeviceFlow]
Driver           = Trino ODBC
hostname         = https://localhost
port             = 8443
sslCaFile        = $(pwd)/$OUT/server.crt
authmethod       = Device Flow
oidcDiscoveryUrl = http://localhost:8180/realms/trino/.well-known/openid-configuration
clientId         = odbc-device
oidcScope        = openid

; Negative cases: no credentials / untrusted certificate.
[TrinoSecureNoAuth]
Driver     = Trino ODBC
hostname   = https://localhost
port       = 8443
sslCaFile  = $(pwd)/$OUT/server.crt
authmethod = No Auth

[TrinoSecureUntrusted]
Driver     = Trino ODBC
hostname   = https://localhost
port       = 8443
authmethod = No Auth

; Password authentication; the password comes from the application (PWD).
[TrinoSecurePassword]
Driver     = Trino ODBC
hostname   = https://localhost
port       = 8443
sslCaFile  = $(pwd)/$OUT/server.crt
authmethod = Password
user       = $TRINO_TEST_USER
INI
echo "Generated $OUT/odbc.ini and $OUT/odbcinst.ini (driver: $DRIVER_LIB)"
