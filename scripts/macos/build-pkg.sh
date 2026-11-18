#!/bin/sh
# Builds the macOS installer: out/pkg/TrinoODBC-<version>.pkg.
#
#   scripts/macos/build-pkg.sh [options]
#
# Options:
#   --lib PATH        universal driver dylib
#                     (default: out/build/macos-universal/libTrinoODBC.dylib)
#   --version X.Y.Z   package version (default: from git describe)
#   --iodbc-dmg PATH  OpenLink's iODBC SDK disk image (default: downloaded
#                     from its GitHub release and checked against a pinned
#                     SHA-256)
#   --sign IDENTITY   "Developer ID Installer: ..." identity to sign with
#                     (default: unsigned)
#   --out DIR         output directory (default: out/pkg)
#
# The package installs the driver into /Library/ODBC/TrinoODBC and registers
# it, plus OpenLink's iODBC frameworks and iODBC Administrator, taken
# unchanged from the official iODBC SDK, when they aren't installed yet
# (Excel for Mac needs the frameworks).
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LIB="$REPO_ROOT/out/build/macos-universal/libTrinoODBC.dylib"
VERSION=""
IODBC_DMG=""
SIGN_IDENTITY=""
OUT_DIR="$REPO_ROOT/out/pkg"

IODBC_URL="https://github.com/openlink/iODBC/releases/download/v3.52.16/iODBC-SDK-3.52.16-macOS11.dmg"
IODBC_SHA256="a7aa117970f172425c7a8df9cc85a86dbd28ecf73823bf2d8ef3f15212bf531b"

while [ $# -gt 0 ]; do
  case "$1" in
    --lib) LIB="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --iodbc-dmg) IODBC_DMG="$2"; shift 2 ;;
    --sign) SIGN_IDENTITY="$2"; shift 2 ;;
    --out) OUT_DIR="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -f "$LIB" ]; then
  echo "Driver not found: $LIB (build the macos-universal preset)" >&2
  exit 1
fi
archs=$(lipo -archs "$LIB")
case "$archs" in
  *arm64*x86_64* | *x86_64*arm64*) ;;
  *) echo "The driver must be universal (arm64 + x86_64), got: $archs" >&2
     exit 1 ;;
esac
if [ -z "$VERSION" ]; then
  VERSION=$(git -C "$REPO_ROOT" describe --tags --always 2>/dev/null || true)
  VERSION=${VERSION#v}
  VERSION=${VERSION:-0.0.0}
fi
case "$VERSION" in
  *[!A-Za-z0-9._-]*) echo "Invalid version: $VERSION" >&2; exit 2 ;;
esac

WORK=$(mktemp -d)
MOUNT="$WORK/iodbc-mount"
cleanup() {
  if [ -d "$MOUNT" ]; then
    hdiutil detach -quiet "$MOUNT" 2>/dev/null || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# --- iODBC components, unchanged from the signed OpenLink SDK -------------
if [ -z "$IODBC_DMG" ]; then
  IODBC_DMG="$WORK/iODBC-SDK.dmg"
  curl -fsSL --retry 3 -o "$IODBC_DMG" "$IODBC_URL"
fi
actual=$(shasum -a 256 "$IODBC_DMG" | cut -d' ' -f1)
if [ "$actual" != "$IODBC_SHA256" ]; then
  echo "iODBC SDK checksum mismatch: $actual" >&2
  exit 1
fi
mkdir -p "$MOUNT"
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$MOUNT" "$IODBC_DMG"
pkgutil --check-signature "$MOUNT/iODBC-SDK.pkg" |
  grep -q "Developer ID Installer: OpenLink Software Inc." || {
  echo "iODBC SDK package is not signed by OpenLink" >&2
  exit 1
}
pkgutil --expand "$MOUNT/iODBC-SDK.pkg" "$WORK/iodbc"
mkdir -p "$WORK/packages"
pkgutil --flatten "$WORK/iodbc/iODBCframe.pkg" \
  "$WORK/packages/iodbc-frameworks.pkg"
pkgutil --flatten "$WORK/iodbc/iODBCapps64.pkg" \
  "$WORK/packages/iodbc-administrator64.pkg"

# --- The driver component --------------------------------------------------
ROOT="$WORK/root"
DEST="$ROOT/Library/ODBC/TrinoODBC"
mkdir -p "$DEST" "$WORK/scripts"
install -m 755 "$LIB" "$DEST/libTrinoODBC.dylib"
install -m 755 "$REPO_ROOT/scripts/macos/pkg/uninstall.sh" "$DEST/uninstall.sh"
install -m 644 "$REPO_ROOT/scripts/macos/lib/odbc-ini.sh" "$DEST/odbc-ini.sh"
install -m 644 "$REPO_ROOT/LICENSE" "$DEST/LICENSE"
install -m 644 "$REPO_ROOT/NOTICE" "$DEST/NOTICE"
install -m 755 "$REPO_ROOT/scripts/macos/pkg/scripts/postinstall" \
  "$WORK/scripts/postinstall"
install -m 644 "$REPO_ROOT/scripts/macos/lib/odbc-ini.sh" \
  "$WORK/scripts/odbc-ini.sh"

# Drop extended attributes (quarantine etc.); pkgbuild would store them as
# ._ entries. macOS 15 may keep com.apple.provenance, which is harmless.
xattr -cr "$ROOT" "$WORK/scripts" 2>/dev/null || true
COPYFILE_DISABLE=1 pkgbuild --quiet \
  --root "$ROOT" \
  --scripts "$WORK/scripts" \
  --identifier io.github.freakingmind.odbc-for-trino \
  --version "$VERSION" \
  --install-location / \
  "$WORK/packages/trino-odbc-driver.pkg"

# --- The product archive ---------------------------------------------------
mkdir -p "$WORK/resources"
cp "$REPO_ROOT/scripts/macos/pkg/resources/conclusion.html" "$WORK/resources/"
# The license page: ours, then iODBC's (BSD), taken from its package.
mkdir -p "$WORK/iodbc-payload"
(cd "$WORK/iodbc-payload" &&
  gunzip -c "$WORK/iodbc/iODBCframe.pkg/Payload" |
  cpio -id --quiet "./Library/Application Support/iODBC/LICENSE*")
IODBC_LICENSE_DIR="$WORK/iodbc-payload/Library/Application Support/iODBC"
{
  cat "$REPO_ROOT/LICENSE"
  printf '\n\n==== iODBC driver manager (installed when missing) ====\n\n'
  cat "$IODBC_LICENSE_DIR/LICENSE"
  printf '\n'
  cat "$IODBC_LICENSE_DIR/LICENSE.BSD"
} > "$WORK/resources/LICENSE.txt"
sed "s/@VERSION@/$VERSION/" "$REPO_ROOT/scripts/macos/pkg/distribution.xml" \
  > "$WORK/distribution.xml"

mkdir -p "$OUT_DIR"
OUTPUT="$OUT_DIR/TrinoODBC-$VERSION.pkg"
if [ -n "$SIGN_IDENTITY" ]; then
  productbuild --quiet --distribution "$WORK/distribution.xml" \
    --resources "$WORK/resources" --package-path "$WORK/packages" \
    --sign "$SIGN_IDENTITY" "$OUTPUT"
else
  productbuild --quiet --distribution "$WORK/distribution.xml" \
    --resources "$WORK/resources" --package-path "$WORK/packages" \
    "$OUTPUT"
fi
echo "Built $OUTPUT"
