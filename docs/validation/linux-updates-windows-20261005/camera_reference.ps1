$ErrorActionPreference='Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$taskRoot='H:/Git/coin/build/linux-updates-windows-20261005'
$env:COIN_RENDER_REQUIRE_CAMERA_REFERENCE='1'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE='1'
foreach ($taskOption in @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(COIN_(BGFX|WGPU|RENDER)_DISABLE_|COIN_(WGPU|RENDER)_TRACE_|COIN_BGFX_READBACK_PIPELINE_DEPTH|COIN_WGPU_CAMERA_BINDINGS)' } | Select-Object -ExpandProperty Name)) {
  Remove-Item "Env:$taskOption" -ErrorAction SilentlyContinue
}
$taskFailed=$false
foreach ($backend in @('bgfx','wgpu')) {
  $apis=if ($backend -eq 'bgfx') { @('opengl','vulkan','d3d12') } else { @('gl','vulkan','dx12') }
  foreach ($api in $apis) {
    if ($backend -eq 'bgfx') { $env:COIN_BGFX_RENDERER=$api } else { $env:WGPU_BACKEND=$api }
    ctest --test-dir "build/coin-render-$backend-msvc" -C Release --parallel 1 --output-on-failure -R '^CoinRenderCameraReuseReferenceTest$' --output-junit "$taskRoot/tests-camera-$backend-$api.xml" *> "$taskRoot/tests-camera-$backend-$api.log"
    Get-Content "$taskRoot/tests-camera-$backend-$api.log" -Tail 9
    if ($LASTEXITCODE -ne 0) { $taskFailed=$true }
  }
}
if ($taskFailed) { throw 'Camera reference comparisons failed; inspect logs.' }
