#!/usr/bin/env bash

set -euo pipefail

readonly renderer="${1:?usage: build-and-test-bgfx-gpu.sh <vulkan|opengl>}"
case "${renderer}" in
  vulkan|opengl) ;;
  *)
    echo "Renderer must be vulkan or opengl, got: ${renderer}" >&2
    exit 2
    ;;
esac

readonly expected_vendor_pattern="${COIN_CI_GPU_VENDOR_PATTERN:?set COIN_CI_GPU_VENDOR_PATTERN}"
readonly expected_driver_pattern="${COIN_CI_GPU_DRIVER_PATTERN:?set COIN_CI_GPU_DRIVER_PATTERN}"
readonly repository_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly build_dir="${COIN_CI_BUILD_DIR:-${repository_dir}/cmake-bgfx-${renderer}}"
readonly evidence_dir="${COIN_CI_EVIDENCE_DIR:-${repository_dir}/gpu-matrix-evidence}"
readonly bgfx_prefix="${COIN_BGFX_PREFIX:?set COIN_BGFX_PREFIX to the BGFX installation prefix}"
readonly shaderc="${COIN_BGFX_SHADERC_EXECUTABLE:-${bgfx_prefix}/bin/shaderc}"
readonly shader_include="${COIN_BGFX_SHADER_INCLUDE_DIR:-${bgfx_prefix}/include/bgfx}"
readonly inventory_file="${evidence_dir}/${renderer}-inventory.txt"
readonly capabilities_file="${evidence_dir}/${renderer}-capabilities.txt"
readonly tests_file="${evidence_dir}/${renderer}-tests.txt"

if [[ -z "${DISPLAY:-}" ]]; then
  echo "A hardware-backed X11 DISPLAY is required; Xvfb/software rendering is not accepted" >&2
  exit 2
fi
for command_name in cmake ninja grep tee; do
  command -v "${command_name}" >/dev/null || {
    echo "Missing required command: ${command_name}" >&2
    exit 2
  }
done
[[ -x "${shaderc}" ]] || { echo "shaderc is not executable: ${shaderc}" >&2; exit 2; }
[[ -f "${shader_include}/bgfx_shader.sh" ]] || {
  echo "BGFX shader include not found: ${shader_include}/bgfx_shader.sh" >&2
  exit 2
}

mkdir -p "${evidence_dir}"
export COIN_BGFX_RENDERER="${renderer}"
if [[ "${renderer}" == vulkan && -z "${VK_DRIVER_FILES:-}${VK_ICD_FILENAMES:-}" ]]; then
  echo "Set VK_DRIVER_FILES or VK_ICD_FILENAMES so vulkaninfo and BGFX use one explicit ICD" >&2
  exit 2
fi

case "${renderer}" in
  vulkan)
    command -v vulkaninfo >/dev/null || { echo "Missing required command: vulkaninfo" >&2; exit 2; }
    {
      echo "renderer=vulkan"
      echo "display=${DISPLAY}"
      echo "vulkan_driver_files=${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}"
      echo "uname=$(uname -a)"
      vulkaninfo --summary
    } 2>&1 | tee "${inventory_file}"
    ;;
  opengl)
    command -v glxinfo >/dev/null || { echo "Missing required command: glxinfo" >&2; exit 2; }
    {
      echo "renderer=opengl"
      echo "display=${DISPLAY}"
      echo "uname=$(uname -a)"
      glxinfo -B
    } 2>&1 | tee "${inventory_file}"
    ;;
esac

if ! grep -Eiq -- "${expected_vendor_pattern}" "${inventory_file}"; then
  echo "GPU vendor inventory did not match: ${expected_vendor_pattern}" >&2
  exit 1
fi
if ! grep -Eiq -- "${expected_driver_pattern}" "${inventory_file}"; then
  echo "GPU driver inventory did not match: ${expected_driver_pattern}" >&2
  exit 1
fi
if grep -Eiq -- "llvmpipe|softpipe|lavapipe|swiftshader" "${inventory_file}"; then
  echo "Software rasterizer detected; refusing to certify a hardware matrix cell" >&2
  exit 1
fi

cmake -S "${repository_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${bgfx_prefix}" \
  -DCOIN_BUILD_WGPU=ON \
  -DCOIN_WGPU_BACKEND=BGFX \
  -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_WGPU_WINDOW_EXAMPLE=ON \
  -DCOIN_BUILD_LEGACY_GL_RENDERER=ON \
  -DCOIN_STRICT_WARNINGS=ON \
  -DCOIN_BGFX_SHADERC_EXECUTABLE="${shaderc}" \
  -DCOIN_BGFX_SHADER_INCLUDE_DIR="${shader_include}"
cmake --build "${build_dir}" --config Release --parallel --target \
  BgfxReadbackModesTest \
  BgfxSurfaceFeaturesTest \
  WgpuFogTest \
  WgpuProductTest \
  WgpuBackendContractTest \
  WgpuBgfxCoreTest \
  WgpuBgfxOffscreenTest \
  WgpuBgfxTransparencyTest \
  WgpuBgfxWindowTest \
  WgpuBgfxMultiWindowTest

if [[ "${renderer}" == vulkan ]]; then
  readonly test_regex="^(BgfxReadbackModes_vulkan|BgfxSurface_vulkan_(object|weighted_oit|sorted_layers)|WgpuFogTest|WgpuProductTest|WgpuBackendContractTest|WgpuBgfxCoreTest|WgpuBgfxOffscreenTest|WgpuBgfxTransparencyTest|WgpuBgfxSortedLayersTest|WgpuBgfxWeightedOitTest|WgpuBgfxWindowTest|WgpuBgfxRenderManagerAdapterTest|WgpuBgfxMultiWindowTest|WgpuBgfxMultiWindowOffscreenFirstTest|WgpuBgfxMultiWindow_(weighted_oit|sorted_layers)_vulkanTest)$"
else
  readonly test_regex="^(BgfxReadbackModes_opengl|BgfxSurface_opengl_(object|weighted_oit|sorted_layers)|WgpuFogTest|WgpuProductOpenGLTest|WgpuBackendContractOpenGLTest|WgpuBgfxCoreTest|WgpuBgfxOpenGLTest|WgpuBgfxTransparencyOpenGLTest|WgpuBgfxSortedLayersOpenGLTest|WgpuBgfxWeightedOitOpenGLTest|WgpuBgfxWindowOpenGLTest|WgpuBgfxRenderManagerAdapterOpenGLTest|WgpuBgfxMultiWindowOpenGLTest|WgpuBgfxMultiWindowOffscreenFirstOpenGLTest|WgpuBgfxMultiWindow_(weighted_oit|sorted_layers)_openglTest)$"
fi

ctest --test-dir "${build_dir}" -C Release -R "${test_regex}" \
  --output-on-failure 2>&1 | tee "${tests_file}"
"${build_dir}/bin/WgpuProductTest" 2>&1 | tee "${capabilities_file}"

if ! grep -q "COIN_WGPU_CAPABILITIES" "${capabilities_file}"; then
  echo "Capability evidence line is missing" >&2
  exit 1
fi
if [[ "${renderer}" == vulkan ]] && ! grep -q " renderer=1 " "${capabilities_file}"; then
  echo "Coin/BGFX did not report the Vulkan renderer" >&2
  exit 1
fi
if [[ "${renderer}" == opengl ]] && ! grep -q " renderer=2 " "${capabilities_file}"; then
  echo "Coin/BGFX did not report the OpenGL renderer" >&2
  exit 1
fi
if [[ -n "${COIN_CI_GPU_VENDOR_ID:-}" ]] &&
   ! grep -Eiq -- "vendor_id=${COIN_CI_GPU_VENDOR_ID}([[:space:]]|$)" "${capabilities_file}"; then
  echo "Coin/BGFX vendor id did not match: ${COIN_CI_GPU_VENDOR_ID}" >&2
  exit 1
fi

echo "BGFX GPU matrix cell passed: renderer=${renderer} evidence=${evidence_dir}"
