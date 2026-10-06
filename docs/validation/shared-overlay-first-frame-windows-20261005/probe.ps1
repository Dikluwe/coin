$ErrorActionPreference='Stop'
$taskRoot='H:/Git/coin/build/shared-overlay-first-frame-windows-20261005'
$env:COIN_RENDER_TRACE_PHASES='1'
foreach ($backend in @('bgfx','wgpu')) {
  $env:COIN_BGFX_RENDERER='opengl'; $env:WGPU_BACKEND='gl'
  foreach ($revision in @('before','after')) {
    $directory=if ($revision -eq 'before') { "$taskRoot/baseline-$backend" } else { "H:/Git/coin/build/coin-render-$backend-msvc/bin" }
    & "$directory/coin_render_gl_benchmark.exe" --backend $backend --scene H:/Git/coin/build/large-scenes/city-40000.iv --size 1024 --warmup 1 --frames 3 --image-output "$taskRoot/probe-$backend-$revision.ppm" *> "$taskRoot/probe-$backend-$revision.log"
    if ($LASTEXITCODE -ne 0) { Get-Content "$taskRoot/probe-$backend-$revision.log" -Tail 12; throw 'Probe failed' }
    Get-Content "$taskRoot/probe-$backend-$revision.log" | Select-String 'object_qualification|_first_frame_ms='
  }
}
Remove-Item Env:COIN_RENDER_TRACE_PHASES
