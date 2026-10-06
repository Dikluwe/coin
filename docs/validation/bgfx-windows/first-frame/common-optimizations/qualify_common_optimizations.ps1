param([switch]$SkipVulkan)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
foreach ($name in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS',
    'COIN_WGPU_CAMERA_BINDINGS','COIN_WGPU_DISABLE_OPAQUE_BATCHING','COIN_BGFX_DISABLE_DRAW_BATCHING')) {
  Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
$env:WGPU_BACKEND = 'vulkan'
if (!$SkipVulkan) {
  ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --output-junit H:/Git/coin/build/common-opt-wgpu-vulkan-tests.xml *> build/common-opt-wgpu-vulkan-tests.log
  if ($LASTEXITCODE -ne 0) { throw 'Full Vulkan suite failed' }
  Write-Output 'Full wgpu Vulkan suite passed.'
}
$cross = @(Get-Content build/wgpu-large-cross-api-tests.txt) + @('CoinRenderFrameCoreTest','CoinRenderDepthContractTest','CoinRenderShadowReferenceTest')
$cross | Set-Content build/common-opt-cross-api-tests.txt -Encoding ascii
foreach ($api in @('gl','dx12')) {
  $env:WGPU_BACKEND = $api
  ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/common-opt-cross-api-tests.txt --output-junit "H:/Git/coin/build/common-opt-wgpu-$api-tests.xml" *> "build/common-opt-wgpu-$api-tests.log"
  if ($LASTEXITCODE -ne 0) { throw "$api regression suite failed" }
  Write-Output "wgpu $api regression suite passed."
}
Remove-Item Env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
$env:COIN_BGFX_RENDERER = 'vulkan'
ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure --output-junit H:/Git/coin/build/common-opt-bgfx-tests.xml *> build/common-opt-bgfx-tests.log
if ($LASTEXITCODE -ne 0) { throw 'BGFX suite failed' }
Write-Output 'Full BGFX suite passed, including parameterized Vulkan/D3D12/OpenGL cases.'
