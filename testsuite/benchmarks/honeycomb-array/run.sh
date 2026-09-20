#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: $0 /path/to/build/lib construct|bbox|render side [timeout-seconds]" >&2
  exit 2
fi

LIBDIR=$(CDPATH= cd "$1" && pwd)
MODE=$2
SIDE=$3
LIMIT=${4:-60}
HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
SOURCE=$(CDPATH= cd "$HERE/../../.." && pwd)
CXX=${CXX:-c++}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/coin-honeycomb.XXXXXX")
trap 'rm -rf "$OUT"' EXIT HUP INT TERM

"$CXX" ${CXXFLAGS:-} -O2 -g "$HERE/benchmark.cpp" -o "$OUT/benchmark" \
  -I"$SOURCE/include" -I"$LIBDIR/../include" -L"$LIBDIR" \
  ${LDFLAGS:-} -lCoin

export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export COIN_GLX_PIXMAP_DIRECT_RENDERING=1

if [ "$MODE" = construct ]; then
  /usr/bin/time -f 'wall=%e user=%U system=%S max_rss_kib=%M' \
    "$OUT/benchmark" "$MODE" "$SIDE"
  exit $?
fi

/usr/bin/time -f 'wall=%e user=%U system=%S max_rss_kib=%M exit=%x' \
  timeout --signal=TERM --kill-after=5 "${LIMIT}s" \
  "$OUT/benchmark" "$MODE" "$SIDE"
