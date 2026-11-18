#!/bin/sh
# Installs a locally built driver for testing with Excel for Mac (or any
# other iODBC application) and registers it system-wide. For end users,
# build the installer instead: scripts/macos/build-pkg.sh.
#
#   sudo scripts/macos/install-dev-driver.sh [options]
#
# Options:
#   --lib PATH        driver dylib (default: out/build/macos-universal/libTrinoODBC.dylib)
#   --ca-bundle PATH  copy a PEM CA bundle next to the driver (as ca-bundle.pem)
#                     so DSNs can use it as sslCaFile (Excel's sandbox can't
#                     read files in your home directory)
#   --dsn-file PATH   append the DSN sections from this odbc.ini fragment
#                     (sections already present are skipped)
#
# Requirements: the iODBC framework (/Library/Frameworks/iODBC.framework,
# from OpenLink's iODBC SDK), which Excel for Mac uses as driver manager.
#
# Excel is sandboxed: it can load the driver from /Library/ODBC and read the
# system-wide /Library/ODBC/odbc.ini and odbcinst.ini.
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LIB="$REPO_ROOT/out/build/macos-universal/libTrinoODBC.dylib"
CA_BUNDLE=""
DSN_FILE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --lib) LIB="$2"; shift 2 ;;
    --ca-bundle) CA_BUNDLE="$2"; shift 2 ;;
    --dsn-file) DSN_FILE="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo: it writes to /Library/ODBC." >&2
  exit 1
fi
if [ ! -d /Library/Frameworks/iODBC.framework ]; then
  echo "iODBC framework not found. Install OpenLink's iODBC SDK first." >&2
  exit 1
fi
if [ ! -f "$LIB" ]; then
  echo "Driver not found: $LIB (build the macos-universal preset)" >&2
  exit 1
fi

. "$REPO_ROOT/scripts/macos/lib/odbc-ini.sh"

mkdir -p "$DRIVER_DIR"
install -m 755 "$LIB" "$DRIVER_DIR/libTrinoODBC.dylib"
install -m 755 "$REPO_ROOT/scripts/macos/pkg/uninstall.sh" "$DRIVER_DIR/uninstall.sh"
install -m 644 "$REPO_ROOT/scripts/macos/lib/odbc-ini.sh" "$DRIVER_DIR/odbc-ini.sh"
# A downloaded build may carry the quarantine flag, which blocks loading.
xattr -d com.apple.quarantine "$DRIVER_DIR/libTrinoODBC.dylib" 2>/dev/null || true
echo "Installed $DRIVER_DIR/libTrinoODBC.dylib"

if [ -n "$CA_BUNDLE" ]; then
  install -m 644 "$CA_BUNDLE" "$DRIVER_DIR/ca-bundle.pem"
  echo "Installed $DRIVER_DIR/ca-bundle.pem"
fi

register_driver
echo "Registered driver '$DRIVER_NAME' in $INST_INI"

# A DSN for the local Docker test environment (docker/compose.yml).
if ! has_section "$DSN_INI" "TrinoLocal"; then
  ensure_list_entry "$DSN_INI" "ODBC Data Sources" "TrinoLocal" "$DRIVER_NAME"
  cat >> "$DSN_INI" <<EOF

[TrinoLocal]
Driver     = $DRIVER_NAME
hostname   = http://localhost
port       = 8080
authmethod = No Auth
loglevel   = Debug
EOF
  echo "Added DSN TrinoLocal (local Docker Trino) to $DSN_INI"
fi

# Extra DSNs from a local fragment (e.g. for a real server; keep such files
# out of version control).
if [ -n "$DSN_FILE" ]; then
  sections=$(mktemp)
  sed -n 's/^\[\(.*\)\][[:space:]]*$/\1/p' "$DSN_FILE" > "$sections"
  while IFS= read -r section; do
    [ "$section" = "ODBC Data Sources" ] && continue
    if has_section "$DSN_INI" "$section"; then
      echo "DSN [$section] already exists in $DSN_INI, skipped"
      continue
    fi
    ensure_list_entry "$DSN_INI" "ODBC Data Sources" "$section" "$DRIVER_NAME"
    printf '\n' >> "$DSN_INI"
    awk -v s="[$section]" '
        $0 == s { inside = 1; print; next }
        /^\[/ { inside = 0 }
        inside { print }' "$DSN_FILE" >> "$DSN_INI"
    echo "Added DSN [$section] to $DSN_INI"
  done < "$sections"
  rm -f "$sections"
fi
