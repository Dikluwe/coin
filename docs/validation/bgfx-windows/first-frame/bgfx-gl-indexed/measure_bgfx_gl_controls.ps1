$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS',
  'COIN_WGPU_CAMERA_BINDINGS','COIN_WGPU_DISABLE_OPAQUE_BATCHING')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
$runs = @()
foreach ($backend in @('gl','wgpu')) {
  $apis = if ($backend -eq 'gl') { @('opengl') } else { @('gl','vulkan') }
  foreach ($api in $apis) {
    $env:WGPU_BACKEND = $api
    for ($sample = 1; $sample -le 3; ++$sample) {
      $tag = "bgfx-gl-indexed-control-$backend-$api-$sample"
      & build/coin-render-wgpu-msvc/bin/coin_render_gl_benchmark.exe --scene build/large-scenes/city-40000.iv --backend $backend --size 1024 --warmup 4 --frames 8 *> "build/$tag.log"
      $exitCode = $LASTEXITCODE
      $script:runs += [pscustomobject]@{backend=$backend;api=$api;sample=$sample;exit_code=$exitCode;log="build/$tag.log"}
      $script:runs | ConvertTo-Json -Depth 4 | Set-Content build/bgfx-gl-indexed-controls.json -Encoding UTF8
      Select-String -Path "build/$tag.log" -Pattern 'first_frame_ms=|frames=8 median_ms='
      if ($exitCode -ne 0) { throw "$tag failed" }
    }
  }
}
