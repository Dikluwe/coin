#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "Usage: bash reproduce.sh COIN_SOURCE COIN_BUILD RESULTS" >&2
  exit 2
fi
study_dir=$(cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(realpath "$1")
mkdir -p "$2" "$3"
build_dir=$(realpath "$2")
results_dir=$(realpath "$3")
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build "$build_dir" -j8
c++ -std=c++11 "$study_dir/probe.cpp" \
  -I"$build_dir/include" -I"$source_dir/include" \
  -L"$build_dir/lib" -Wl,-rpath,"$build_dir/lib" -lCoin \
  -o "$build_dir/issue417-probe"
cd "$results_dir"
xvfb-run -a env COIN_GLX_PIXMAP_DIRECT_RENDERING=1 LIBGL_ALWAYS_SOFTWARE=1 \
  "$build_dir/issue417-probe" | tee probe.log
