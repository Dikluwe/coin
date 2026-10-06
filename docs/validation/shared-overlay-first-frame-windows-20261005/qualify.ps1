$ErrorActionPreference='Stop'
$taskRoot='H:/Git/coin/build/shared-overlay-first-frame-windows-20261005'
foreach ($taskOption in @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(COIN_(BGFX|WGPU|RENDER)_DISABLE_|COIN_(WGPU|RENDER)_TRACE_|COIN_BGFX_READBACK_PIPELINE_DEPTH|COIN_WGPU_CAMERA_BINDINGS)' } | Select-Object -ExpandProperty Name)) {
  Remove-Item "Env:$taskOption" -ErrorAction SilentlyContinue
}
$env:COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE='1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE='1'
$env:COIN_BGFX_TRANSPARENCY='auto'
$tests=@('CoinRenderActionTest','CoinRenderFrameCoreTest','CoinRenderFrameReuseCoreTest',
 'CoinRenderIndexedFastPathTest','CoinRenderTextureTest','CoinRenderLightingTest',
 'CoinRenderGouraudInterpolationTest','CoinRenderFogTest','CoinRenderClipPlaneTest',
 'CoinRenderCompositionTest','CoinRenderBackendBoundaryTest',
 'CoinRenderRuntimeBoundaryTest','CoinRenderPlanAssemblyCoreTest','CoinRenderTransformCoreTest',
 'CoinRenderCameraReuseReferenceTest','CoinRenderBenchmarkAnimationTest')
foreach ($backend in @('bgfx','wgpu')) {
  $selected=$tests
  if ($backend -eq 'wgpu') { $selected += 'CoinRenderAnnotationTest' }
  $selected | Set-Content "$taskRoot/selected-$backend-tests.txt"
  $apis=if ($backend -eq 'bgfx') { @('opengl','vulkan','d3d12') } else { @('gl','vulkan','dx12') }
  foreach ($api in $apis) {
    if ($backend -eq 'bgfx') { $env:COIN_BGFX_RENDERER=$api } else { $env:WGPU_BACKEND=$api }
    ctest --test-dir "H:/Git/coin/build/coin-render-$backend-msvc" -C Release --parallel 1 --output-on-failure --tests-from-file "$taskRoot/selected-$backend-tests.txt" --output-junit "$taskRoot/tests-$backend-$api.xml" *> "$taskRoot/tests-$backend-$api.log"
    Get-Content "$taskRoot/tests-$backend-$api.log" -Tail 9
    if ($LASTEXITCODE -ne 0) { throw "Regression failed: $backend/$api" }
    [xml]$result=Get-Content "$taskRoot/tests-$backend-$api.xml" -Raw
    if (@($result.testsuite.testcase).Count -ne $selected.Count -or [int]$result.testsuite.skipped -ne 0) { throw "Incomplete regression: $backend/$api" }
  }
}
