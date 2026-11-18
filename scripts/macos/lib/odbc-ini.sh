# Helpers for editing odbc.ini / odbcinst.ini, sourced by the macOS install
# scripts (install-dev-driver.sh, the .pkg postinstall, uninstall.sh).
# POSIX sh. Section and key names are compared literally, ignoring
# surrounding whitespace and CR (files edited on Windows); values are passed
# through the environment so awk doesn't interpret backslashes.

DRIVER_NAME="Trino ODBC"
ODBC_DIR=/Library/ODBC
DRIVER_DIR="$ODBC_DIR/TrinoODBC"
INST_INI="$ODBC_DIR/odbcinst.ini"
DSN_INI="$ODBC_DIR/odbc.ini"

# shellcheck disable=SC2016 # awk code, expanded by awk
# The awk prelude shared by the functions below: t is the trimmed line,
# header is the section name when the line is "[name]".
_INI_AWK_PRELUDE='
function trim(s) { sub(/^[ \t\r]+/, "", s); sub(/[ \t\r]+$/, "", s); return s }
{ t = trim($0); header = "" }
t ~ /^\[.*\]$/ { header = substr(t, 2, length(t) - 2) }
'

# shellcheck disable=SC2016
# The key of a "key = value" line (trimmed), or "" for other lines.
_INI_AWK_KEY='
function key_of(s,   i) { i = index(s, "="); return i ? trim(substr(s, 1, i - 1)) : "" }
'

has_section() {
  INI_SECTION=$2 awk "$_INI_AWK_PRELUDE"'
      header != "" && header == ENVIRON["INI_SECTION"] { found = 1 }
      END { exit !found }' "$1"
}

# Rewrites $1 with the output of an awk program (keeps owner and mode).
_ini_rewrite() {
  _ir_file=$1
  shift
  _ir_tmp=$(mktemp)
  awk "$@" "$_ir_file" > "$_ir_tmp"
  cat "$_ir_tmp" > "$_ir_file"
  rm -f "$_ir_tmp"
}

# Adds "key = value" under [section] unless the key is already present.
ensure_list_entry() {
  if ! has_section "$1" "$2"; then
    printf '\n[%s]\n' "$2" >> "$1"
  fi
  if INI_SECTION=$2 INI_KEY=$3 awk "$_INI_AWK_PRELUDE$_INI_AWK_KEY"'
      header != "" { inside = (header == ENVIRON["INI_SECTION"]); next }
      inside && key_of(t) == ENVIRON["INI_KEY"] { found = 1 }
      END { exit !found }' "$1"; then
    return 0
  fi
  INI_SECTION=$2 INI_LINE="$3 = $4" _ini_rewrite "$1" "$_INI_AWK_PRELUDE"'
      { print }
      header != "" && header == ENVIRON["INI_SECTION"] && !done {
        print ENVIRON["INI_LINE"]; done = 1
      }'
}

# Removes [section] and its keys.
remove_section() {
  INI_SECTION=$2 _ini_rewrite "$1" "$_INI_AWK_PRELUDE"'
      header != "" { skip = (header == ENVIRON["INI_SECTION"]) }
      !skip { print }'
}

# Removes "key = ..." lines under [section].
remove_list_entry() {
  INI_SECTION=$2 INI_KEY=$3 _ini_rewrite "$1" "$_INI_AWK_PRELUDE$_INI_AWK_KEY"'
      header != "" { inside = (header == ENVIRON["INI_SECTION"]); print; next }
      inside && key_of(t) == ENVIRON["INI_KEY"] { next }
      { print }'
}

# Registers the driver in odbcinst.ini (idempotent). When it creates
# odbc.ini, makes it writable for administrators so that the iODBC
# Administrator can add system DSNs without sudo; an existing file keeps its
# owner and mode.
register_driver() {
  mkdir -p "$DRIVER_DIR"
  if [ ! -e "$DSN_INI" ]; then
    : > "$DSN_INI"
    chown root:admin "$DSN_INI"
    chmod 664 "$DSN_INI"
  fi
  if [ ! -e "$INST_INI" ]; then
    : > "$INST_INI"
    chmod 644 "$INST_INI"
  fi
  ensure_list_entry "$INST_INI" "ODBC Drivers" "$DRIVER_NAME" "Installed"
  if ! has_section "$INST_INI" "$DRIVER_NAME"; then
    printf '\n[%s]\nDriver = %s\nSetup  = %s\n' "$DRIVER_NAME" \
      "$DRIVER_DIR/libTrinoODBC.dylib" "$DRIVER_DIR/libTrinoODBC.dylib" \
      >> "$INST_INI"
  fi
}
