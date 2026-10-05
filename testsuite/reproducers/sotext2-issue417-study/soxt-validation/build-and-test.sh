#!/usr/bin/env bash
set -euo pipefail
study_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
soxt_source=${SOXT_SOURCE:-/repos/GitHub/soxt}
coin_build=${COIN_BUILD:-/tmp/coin-issue417-branch-build}
task_dir=/tmp/coin-issue417-study
prefix="$task_dir/coin-soxt-prefix"
mkdir -p "$task_dir"
cmake --install "$coin_build" --prefix "$prefix"
coin_config=$(rg --files "$prefix" | rg '/coin-config.cmake$')
cmake -S "$soxt_source" -B "$task_dir/soxt-build" \
  -DCMAKE_BUILD_TYPE=Release -DCoin_DIR="$(dirname -- "$coin_config")" \
  -DSOXT_BUILD_TESTS=OFF -DSOXT_BUILD_DOCUMENTATION=OFF
cmake --build "$task_dir/soxt-build" -j8
compile_example() {
  local version_source="$1" version_build="$2" binary="$3"
  c++ -std=c++11 "$study_dir/original-soxt-example.cpp" \
    -I"$version_build/src" -I"$version_source/src" -I"$prefix/include" \
    -L"$version_build/lib" -L"$coin_build/lib" \
    -Wl,-rpath,"$version_build/lib" -Wl,-rpath,"$coin_build/lib" \
    -lSoXt -lCoin -lXt -lXm -lX11 -lGL -o "$binary"
}
compile_example "$soxt_source" "$task_dir/soxt-build" "$task_dir/original-soxt-example"
source140="$task_dir/soxt-1.4.0-src"
mkdir -p "$source140"
git -C "$soxt_source" archive SoXt-1.4.0 | tar -x -C "$source140"
mkdir -p "$source140/data" "$source140/src/Inventor/Xt/common" "$source140/cpack.d"
git -C "$soxt_source/data" archive soanydata-1_1_0 | tar -x -C "$source140/data"
git -C "$soxt_source/src/Inventor/Xt/common" archive soxt-1_4_0 | tar -x -C "$source140/src/Inventor/Xt/common"
git -C "$soxt_source/cpack.d" archive cpack.d-1_0_0 | tar -x -C "$source140/cpack.d"
cmake -S "$source140" -B "$task_dir/soxt-1.4.0-build" \
  -DCMAKE_BUILD_TYPE=Release -DCoin_DIR="$(dirname -- "$coin_config")" \
  -DSOXT_BUILD_DOCUMENTATION=OFF
cmake --build "$task_dir/soxt-1.4.0-build" -j8
compile_example "$source140" "$task_dir/soxt-1.4.0-build" "$task_dir/original-soxt-1.4.0-example"
# The capture harness uses the preserved master build and branch build in /tmp.
# It records /proc/PID/maps and captures the unmodified application's window.
xvfb-run -a -s '-screen 0 1024x768x24' python3 "$study_dir/soxt-validation/capture.py" 1.4.0
xvfb-run -a -s '-screen 0 1024x768x24' python3 "$study_dir/soxt-validation/capture.py" 1.4.2
