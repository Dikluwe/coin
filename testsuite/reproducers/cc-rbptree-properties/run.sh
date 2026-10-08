#!/usr/bin/env bash
set -euo pipefail

coin_root="$(cd "$(dirname "$0")/../../.." && pwd)"
coin_build_dir="$(mktemp -d /dev/shm/coin-rbptree-properties.XXXXXX)"
trap 'rm -rf "$coin_build_dir"' EXIT

cd "$coin_root"
"${CXX:-c++}" -std=c++11 -O1 -g -DCOIN_INTERNAL \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -Iinclude -Isrc \
  testsuite/reproducers/cc-rbptree-properties/repro.cpp \
  -Wl,--gc-sections -o "$coin_build_dir/properties"

# LeakSanitizer cannot attach to processes in this test environment.
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 "$coin_build_dir/properties"

"${CXX:-c++}" -std=c++11 -O1 -g -DCOIN_INTERNAL \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -Iinclude -Isrc \
  testsuite/reproducers/cc-rbptree-properties/reinsert_same_pair.cpp \
  -Wl,--gc-sections -o "$coin_build_dir/reinsert-same-pair"

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 "$coin_build_dir/reinsert-same-pair"
