#!/usr/bin/env bash

set -euo pipefail

readonly repository_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly build_dir="${COIN_CI_BUILD_DIR:-${repository_dir}/cmake_build_dir}"
readonly install_dir="${COIN_CI_INSTALL_DIR:-${repository_dir}/cmake_install_dir}"
readonly smoke_build_dir="${COIN_CI_SMOKE_BUILD_DIR:-${repository_dir}/installed-package-smoke}"

# 1. Configure Coin with WebGPU (Rust Bridge), strict X11 display testing, and window example
cmake_options=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="${install_dir}"
  -DCOIN_BUILD_WGPU=ON
  -DCOIN_WGPU_BACKEND=RUST_BRIDGE
  -DCOIN_TEST_WGPU_STRICT_DISPLAY=ON
  -DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON
  -DCOIN_BUILD_TESTS=ON
  -DCOIN_STRICT_WARNINGS=ON
)

cmake -S "${repository_dir}" -B "${build_dir}" -G Ninja \
  "${cmake_options[@]}"

# 2. Build Coin and install
cmake --build "${build_dir}" --target install --config Release --parallel

# 3. Run all tests under xvfb with strict X11 display requirement
xvfb-run -a ctest --test-dir "${build_dir}" -C Release --output-on-failure

# 4. Run the native window example under xvfb (renders 60 frames to native X11 window)
xvfb-run -a "${build_dir}/bin/coin_render_window_cone" --frames 60

# 5. Verify installed package smoke test
cmake -S "${repository_dir}/testsuite/installed-package-smoke" \
  -B "${smoke_build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${install_dir}" \
  -DCOIN_EXPECT_LEGACY_GL_RENDERER=ON \
  -DCOIN_EXPECT_WGPU=ON \
  -DCOIN_EXPECT_WGPU_WINDOW_SURFACE=ON
cmake --build "${smoke_build_dir}" --config Release --parallel
