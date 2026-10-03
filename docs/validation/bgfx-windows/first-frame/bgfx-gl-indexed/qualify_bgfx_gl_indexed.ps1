$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_BGFX_RENDERER = 'opengl'
$env:COIN_BGFX_TRANSPARENCY = 'auto'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS',
  'COIN_BGFX_DISABLE_DRAW_BATCHING','COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/bgfx-gl-indexed-selected-tests.txt --output-junit H:/Git/coin/build/bgfx-gl-indexed-tests.xml *> build/bgfx-gl-indexed-tests.log
Get-Content build/bgfx-gl-indexed-tests.log -Tail 15
if ($LASTEXITCODE -ne 0) { throw 'BGFX indexed regression failed' }
