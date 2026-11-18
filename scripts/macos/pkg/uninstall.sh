#!/bin/sh
# Removes the Trino ODBC driver installed by the .pkg:
#
#   sudo /Library/ODBC/TrinoODBC/uninstall.sh
#
# DSNs in /Library/ODBC/odbc.ini, the iODBC framework (other drivers may use
# it) and files you added to /Library/ODBC/TrinoODBC (e.g. a CA bundle) stay.
set -eu
if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo." >&2
  exit 1
fi
. "$(dirname "$0")/odbc-ini.sh"
if [ -f "$INST_INI" ]; then
  remove_section "$INST_INI" "$DRIVER_NAME"
  remove_list_entry "$INST_INI" "ODBC Drivers" "$DRIVER_NAME"
fi
for f in libTrinoODBC.dylib LICENSE NOTICE odbc-ini.sh uninstall.sh; do
  rm -f "$DRIVER_DIR/$f"
done
rmdir "$DRIVER_DIR" 2>/dev/null || echo "Kept $DRIVER_DIR (it has other files)."
pkgutil --forget io.github.freakingmind.odbc-for-trino >/dev/null 2>&1 || true
echo "Trino ODBC driver removed."
