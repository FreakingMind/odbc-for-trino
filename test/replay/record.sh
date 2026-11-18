#!/bin/sh
# Records the Trino responses for the "replayable" integration tests, which
# Windows CI replays (see trino_replay.py).
#
#   test/replay/record.sh [build-dir]     (default: out/build/macos-debug)
#
# Needs the Docker test environment (docker/compose.yml; trino-noauth on
# localhost:8080) and a build of the test driver. Re-run it whenever the
# integration tests change, and commit test/replay/recordings.json.gz.
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR=${1:-$REPO_ROOT/out/build/macos-debug}
PORT=18080
OUT="$REPO_ROOT/test/replay/recordings.json.gz"
WORK=$(mktemp -d)

cat > "$WORK/odbc.ini" <<INI
[ODBC Data Sources]
TrinoTestDebug   = Trino ODBC
TrinoTestRelease = Trino ODBC

[TrinoTestDebug]
hostname   = http://localhost
port       = $PORT
authmethod = No Auth

[TrinoTestRelease]
hostname   = http://localhost
port       = $PORT
authmethod = No Auth
INI

python3 "$REPO_ROOT/test/replay/trino_replay.py" record \
  --upstream http://localhost:8080 --port "$PORT" --out "$WORK/recordings.json.gz" &
RECORDER=$!
trap 'kill $RECORDER 2>/dev/null || true; rm -rf "$WORK"' EXIT
sleep 1

status=0
ODBCINI="$WORK/odbc.ini" ctest --test-dir "$BUILD_DIR" -L replay --output-on-failure ||
  status=$?

kill -TERM $RECORDER
wait $RECORDER || true
if [ "$status" -ne 0 ]; then
  echo "Tests failed while recording; recordings not updated." >&2
  exit "$status"
fi
cp "$WORK/recordings.json.gz" "$OUT"
echo "Updated $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"
