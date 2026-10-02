$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
$env:COIN_WGPU_CAMERA_BINDINGS = '1'
$names = @(
  'CoinRenderActionTest','CoinWgpuCacheTest','CoinRenderStabilizationTest',
  'CoinRenderIndexedFastPathTest','CoinRenderMaterialTest','CoinRenderTextureTest',
  'CoinRenderLightingTest','CoinRenderGouraudInterpolationTest','CoinRenderFogTest',
  'CoinRenderClipPlaneTest','CoinRenderDrawStyleTest','CoinRenderMultitextureTest',
  'CoinRenderPeelingTest','CoinRenderWeightedOitTest','CoinRenderTransparencyTest',
  'CoinRenderCompositionTest','CoinRenderAnnotationTest','CoinRenderMultiTargetDirectTest',
  'CoinRenderRttOwnershipTest','CoinRenderSceneTextureDirectTest',
  'CoinRenderSceneTextureBudgetDirectTest','CoinWgpuLargeBindingsTest',
  'CoinRenderPerformanceTest','CoinRenderAsyncReadbackTest','CoinWgpuMultiDeviceTest',
  'WgpuMultiDeviceStressTest','CoinRenderAsyncActionTest',
  'CoinRenderAsyncActionDirectTest','CoinRenderAsyncActionDirectStressTest'
)
$names | Set-Content -LiteralPath build/wgpu-large-cross-api-tests.txt -Encoding utf8
$inventory = & ctest --test-dir build/coin-render-wgpu-msvc -C Release -N
foreach ($name in $names) {
  if (-not ($inventory -match ('\b' + [regex]::Escape($name) + '$'))) {
    throw "Missing test: $name"
  }
}
foreach ($api in @('dx12','gl')) {
  $env:WGPU_BACKEND = $api
  & ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/wgpu-large-cross-api-tests.txt --output-junit "H:/Git/coin/build/wgpu-large-final-$api-tests.xml" *> "build/wgpu-large-final-$api-tests.log"
  if ($LASTEXITCODE -ne 0) { throw "$api CTest failed: $LASTEXITCODE" }
  Write-Output "$api cross-API qualification passed: $($names.Count) tests"
}
