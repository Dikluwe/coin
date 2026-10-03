$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
foreach ($name in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS')) {
  Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$selected = @('CoinRenderActionTest','CoinRenderFrameCoreTest','CoinRenderFrameReuseCoreTest',
  'CoinRenderIndexedFastPathTest','CoinRenderCompositionTest','CoinRenderDepthContractTest',
  'CoinRenderMaterialTest','CoinRenderTextureTest','CoinRenderLightingTest','CoinRenderClipPlaneTest',
  'CoinRenderDrawStyleTest','CoinRenderTransparencyTest','CoinRenderShadowReferenceTest',
  'CoinRenderSceneTextureTest','CoinWgpuFfiFrameTest')
foreach ($flavor in @('wgpu','bgfx')) {
  $apis = if ($flavor -eq 'wgpu') { @('vulkan','gl','dx12') } else { @('vulkan','opengl','d3d12') }
  $available = ctest --test-dir "build/coin-render-$flavor-msvc" -C Release --show-only=json-v1 | ConvertFrom-Json
  $names = @($available.tests.name | Where-Object { $_ -in $selected })
  $names | Set-Content "build/cube-replay-$flavor-tests.txt" -Encoding ascii
  foreach ($api in $apis) {
    $env:WGPU_BACKEND = $api
    $env:COIN_BGFX_RENDERER = $api
    ctest --test-dir "build/coin-render-$flavor-msvc" -C Release --parallel 1 --output-on-failure --tests-from-file "H:/Git/coin/build/cube-replay-$flavor-tests.txt" --output-junit "H:/Git/coin/build/cube-replay-$flavor-$api-tests.xml" *> "build/cube-replay-$flavor-$api-tests.log"
    if ($LASTEXITCODE -ne 0) { throw "$flavor $api cube replay regression failed" }
    Write-Output "$flavor ${api}: $($names.Count) cases passed"
  }
}
