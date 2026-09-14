#!/bin/sh
# Run every invalid-index probe independently against an ASan/UBSan build.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/sanitized/build/lib" >&2
  exit 2
fi

LIBDIR=$(CDPATH= cd "$1" && pwd)
HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
SOURCE=$(CDPATH= cd "$HERE/../../.." && pwd)
CXX=${CXX:-c++}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/coin-sbplist-indices.XXXXXX")
trap 'rm -rf "$OUT"' EXIT HUP INT TERM

case " ${CXXFLAGS:-} ${LDFLAGS:-} " in
  *-fsanitize=*) ;;
  *)
    echo "error: pass sanitizer flags through CXXFLAGS/LDFLAGS" >&2
    exit 2
    ;;
esac

# Intentional splitting lets callers provide multiple compiler/linker flags.
"$CXX" ${CXXFLAGS:-} -O1 -g "$HERE/repro.cpp" -o "$OUT/repro" \
  -I"$SOURCE/include" -I"$LIBDIR/../include" -L"$LIBDIR" \
  ${LDFLAGS:-} -lCoin

export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

modes="
negative-subscript
maximum-subscript
negative-get
negative-set
negative-array-start
negative-insert
negative-remove
negative-remove-fast
negative-truncate
oversize-truncate
"

failures=0
for mode in $modes; do
  logfile="$OUT/$mode.log"
  set +e
  "$OUT/repro" "$mode" >"$logfile" 2>&1
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    echo "FAIL: $mode was not rejected or diagnosed" >&2
    sed -n '1,3p' "$logfile" >&2
    failures=$((failures + 1))
  else
    echo "PASS: $mode was rejected or diagnosed (status $status)"
  fi
done

logfile="$OUT/failed-growth-state.log"
set +e
ASAN_OPTIONS="allocator_may_return_null=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
  "$OUT/repro" failed-growth-state >"$logfile" 2>&1
status=$?
set -e
if [ "$status" -eq 0 ]; then
  echo "PASS: list remained reusable after an injected allocation failure"
else
  echo "FAIL: allocation failure left the list in an inconsistent state" >&2
  sed -n '1,12p' "$logfile" >&2
  failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
  echo "FAIL: $failures SbPList edge-case check(s) failed" >&2
  exit 1
fi

echo "PASS: all invalid SbPList index operations were isolated by diagnostics"
