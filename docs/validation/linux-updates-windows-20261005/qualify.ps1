$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$taskRoot='H:/Git/coin/build/linux-updates-windows-20261005'
foreach ($taskOption in @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(COIN_(BGFX|WGPU|RENDER)_DISABLE_|COIN_(WGPU|RENDER)_TRACE_|COIN_BGFX_READBACK_PIPELINE_DEPTH|COIN_WGPU_GPU_TIMESTAMPS|COIN_WGPU_CAMERA_BINDINGS)' } | Select-Object -ExpandProperty Name)) {
  Remove-Item "Env:$taskOption" -ErrorAction SilentlyContinue
}
$env:COIN_RENDER_REQUIRE_GL_REFERENCE='1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE='1'
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU='1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU='1'
$env:COIN_BGFX_TRANSPARENCY='auto'
$common=@('CoinRenderBackendBoundaryTest','CoinRenderRuntimeBoundaryTest',
  'CoinRenderPlanAssemblyCoreTest','CoinRenderTransformCoreTest',
  'CoinRenderCameraReuseReferenceTest','CoinRenderFrameReuseCoreTest','CoinRenderBenchmarkAnimationTest')
$bgfx=@(Get-Content build/bgfx-gl-stages-selected-tests.txt) + $common + @('CoinBgfxProgramSelectionTest','CoinBgfxInstancingTest','CoinBgfxInstancingOpenGLTest')
$bgfx=$bgfx | Select-Object -Unique
$bgfx | Set-Content "$taskRoot/bgfx-selected.txt"
$env:COIN_BGFX_RENDERER='opengl'
ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure --tests-from-file "$taskRoot/bgfx-selected.txt" --output-junit "$taskRoot/tests-bgfx.xml" *> "$taskRoot/tests-bgfx.log"
Get-Content "$taskRoot/tests-bgfx.log" -Tail 16
$taskFailed=$LASTEXITCODE -ne 0
foreach ($api in @('vulkan','d3d12')) {
  $env:COIN_BGFX_RENDERER=$api
  ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure -R "^CoinBgfxOffscreenTest$|^CoinBgfxReadbackModes_$api`$|^CoinRenderCameraReuseReferenceTest$" --output-junit "$taskRoot/tests-bgfx-$api.xml" *> "$taskRoot/tests-bgfx-$api.log"
  Get-Content "$taskRoot/tests-bgfx-$api.log" -Tail 10
  if ($LASTEXITCODE -ne 0) { $taskFailed=$true }
  if ($api -eq 'd3d12') {
    & build/coin-render-bgfx-msvc/bin/CoinBgfxInstancingTest.exe *> "$taskRoot/instancing-bgfx-d3d12.log"
    if ($LASTEXITCODE -ne 0) { $taskFailed=$true }
  }
}
$wgpu=@(Get-Content build/wgpu-large-cross-api-tests.txt) + $common + @('CoinRenderFrameCoreTest','CoinWgpuFfiFrameTest')
$wgpu=$wgpu | Select-Object -Unique
$wgpu | Set-Content "$taskRoot/wgpu-selected.txt"
foreach ($api in @('gl','vulkan','dx12')) {
  $env:WGPU_BACKEND=$api
  ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --tests-from-file "$taskRoot/wgpu-selected.txt" --output-junit "$taskRoot/tests-wgpu-$api.xml" *> "$taskRoot/tests-wgpu-$api.log"
  Get-Content "$taskRoot/tests-wgpu-$api.log" -Tail 16
  if ($LASTEXITCODE -ne 0) { $taskFailed=$true }
}
if ($taskFailed) { throw 'Some Windows regression cases failed; inspect saved logs.' }
