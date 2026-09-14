#!/bin/sh
# Convenience wrapper for the uninitialized-memory half of the matrix.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 /path/to/msan/build/lib" >&2
  exit 2
fi

HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
CXX=${CXX:-clang++}
CXXFLAGS="${CXXFLAGS:-} -DSBPLIST_NO_ALLOCATION_INTERPOSE -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer"
LDFLAGS="${LDFLAGS:-} -fsanitize=memory"
SKIP_ALLOCATION_TESTS=1
export CXX CXXFLAGS LDFLAGS SKIP_ALLOCATION_TESTS

exec "$HERE/run.sh" "$1"
