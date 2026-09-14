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
past-end-get
past-end-set
negative-array-start
past-end-array-start
negative-insert
past-end-insert
maximum-insert
negative-remove
negative-remove-fast
past-end-remove
past-end-remove-fast
negative-truncate
oversize-truncate
oversize-truncate-within-capacity
minimum-truncate
maximum-truncate
oversize-truncate-fit
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

allocation_modes="
failed-growth-append
failed-growth-append-heap
failed-growth-insert
failed-growth-subscript
failed-growth-distant-subscript
failed-assignment
failed-fit
failed-constructor
failed-copy-constructor
"

if [ "${SKIP_ALLOCATION_TESTS:-0}" -ne 1 ]; then
  for mode in $allocation_modes; do
    logfile="$OUT/$mode.log"
    set +e
    ASAN_OPTIONS="allocator_may_return_null=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
      "$OUT/repro" "$mode" >"$logfile" 2>&1
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
      echo "PASS: $mode preserved a reusable list"
    else
      echo "FAIL: $mode left the list inconsistent" >&2
      sed -n '1,12p' "$logfile" >&2
      failures=$((failures + 1))
    fi
  done

  "$CXX" ${CXXFLAGS:-} -O1 -g "$HERE/callback-allocation.cpp" \
    -o "$OUT/callback-allocation" \
    -I"$SOURCE/include" -I"$LIBDIR/../include" -L"$LIBDIR" \
    ${LDFLAGS:-} -lCoin
  ordinal=1
  while [ "$ordinal" -le 32 ]; do
    logfile="$OUT/failed-callback-assignment-$ordinal.log"
    set +e
    "$OUT/callback-allocation" "$ordinal" >"$logfile" 2>&1
    status=$?
    set -e
    if [ "$status" -eq 77 ]; then
      echo "PASS: exercised every allocation point in callback assignment"
      break
    elif [ "$status" -eq 0 ]; then
      echo "PASS: callback assignment allocation $ordinal preserved all side state"
    else
      echo "FAIL: callback assignment allocation $ordinal split side state" >&2
      sed -n '1,12p' "$logfile" >&2
      failures=$((failures + 1))
    fi
    ordinal=$((ordinal + 1))
  done
  if [ "$ordinal" -gt 32 ]; then
    echo "FAIL: callback allocation matrix exceeded its 32-point safety bound" >&2
    failures=$((failures + 1))
  fi
else
  echo "SKIP: allocation interposition disabled for this sanitizer runtime"
fi

if [ "$failures" -ne 0 ]; then
  echo "FAIL: $failures SbPList edge-case check(s) failed" >&2
  exit 1
fi

echo "PASS: all invalid SbPList index operations were isolated by diagnostics"
