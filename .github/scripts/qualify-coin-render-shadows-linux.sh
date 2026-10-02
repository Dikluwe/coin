#!/usr/bin/env bash
set -euo pipefail

# Use a prepared build and the caller's explicit physical GPU selection.
build_dir="${1:?usage: qualify-coin-render-shadows-linux.sh <build> <bgfx|wgpu> <evidence-dir>}"
backend="${2:?select bgfx or wgpu}"
evidence_dir="${3:?provide an evidence directory}"
expected_vendor="${COIN_SHADOW_GPU_VENDOR_PATTERN:?set the expected physical vendor pattern}"
renderer="${COIN_BGFX_RENDERER:-vulkan}"
[[ -n "${DISPLAY:-}" ]] || { echo 'A physical X11 DISPLAY is required' >&2; exit 2; }
case "${backend}:${renderer}" in
  bgfx:vulkan|bgfx:opengl|wgpu:vulkan) ;;
  *) echo 'Supported cells: BGFX Vulkan/OpenGL and wgpu Vulkan' >&2; exit 2 ;;
esac
mkdir -p "${evidence_dir}"
if [[ "${renderer}" == vulkan ]]; then
  [[ -n "${VK_DRIVER_FILES:-}" ]] || { echo 'Select a single physical Vulkan ICD with VK_DRIVER_FILES' >&2; exit 2; }
  vulkaninfo --summary > "${evidence_dir}/inventory.txt" 2>&1
else
  glxinfo -B > "${evidence_dir}/inventory.txt" 2>&1
fi
if ! rg -iq "${expected_vendor}" "${evidence_dir}/inventory.txt" ||
   rg -iq 'llvmpipe|softpipe|lavapipe|swiftshader' "${evidence_dir}/inventory.txt"; then
  echo 'Physical GPU inventory does not match the requested cell' >&2
  exit 1
fi
"${build_dir}/bin/CoinRenderProductTest" > "${evidence_dir}/capabilities.txt" 2>&1
if [[ "${renderer}" == vulkan ]]; then
  rg -q ' renderer=1 ' "${evidence_dir}/capabilities.txt"
  rg -iq "${expected_vendor}" "${evidence_dir}/capabilities.txt"
else
  rg -q ' renderer=2 ' "${evidence_dir}/capabilities.txt"
fi
if rg -iq 'llvmpipe|softpipe|lavapipe|swiftshader' "${evidence_dir}/capabilities.txt"; then
  echo 'A software adapter cannot qualify this cell' >&2
  exit 1
fi
export COIN_GLXGLUE_NO_PBUFFERS=1
export COIN_GLX_PIXMAP_DIRECT_RENDERING=1
"${build_dir}/bin/CoinRenderShadowReferenceTest" --gl-capacity > "${evidence_dir}/gl-capacity.txt" 2>&1
export COIN_RENDER_REQUIRE_GL_REFERENCE=1
if [[ "${backend}" == bgfx ]]; then
  export COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU=1
else
  export COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU=1
fi
test_regex='^CoinRenderShadow|^CoinRenderRttOwnershipTest$|^CoinRenderSceneTexture(Test|BudgetTest|DirectTest|BudgetDirectTest)$|^CoinRenderSelectionTest$|^CoinRenderDepthContractTest$'
if [[ "${backend}" == bgfx ]]; then
  test_regex+="|^CoinRenderSceneTexture(Budget)?_${renderer}_(staged|direct)$|^CoinBgfxReadbackModes_${renderer}$"
fi
ctest --test-dir "${build_dir}" --output-on-failure -R "${test_regex}" > "${evidence_dir}/tests.txt" 2>&1
if rg -q 'Skipped|Not Run' "${evidence_dir}/tests.txt"; then
  echo 'An explicit physical cell cannot pass with skipped tests' >&2
  exit 1
fi
tail -n 5 "${evidence_dir}/tests.txt"
