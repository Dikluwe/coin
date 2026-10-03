$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$runs = @()
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_WGPU_GPU_TIMESTAMPS',
  'COIN_WGPU_CAMERA_BINDINGS','COIN_WGPU_DISABLE_OPAQUE_BATCHING','COIN_BGFX_DISABLE_DRAW_BATCHING',
  'COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
$env:COIN_BGFX_TRANSPARENCY = 'auto'
function Measure-CommonOptimization($flavor, $api, $count, $variant, $sample, $trace = $false,
    $warmup = 1, $frames = 3, $mode = 'static') {
  $exe = if ($flavor -eq 'coin') { "build/coin-render-wgpu-msvc/bin/coin_render_gl_benchmark.exe" }
    elseif ($variant -eq 'before') { "build/cube-replay-baseline/$flavor/coin_render_gl_benchmark.exe" }
    else { "build/coin-render-$flavor-msvc/bin/coin_render_gl_benchmark.exe" }
  $env:WGPU_BACKEND = $api
  $env:COIN_BGFX_RENDERER = $api
  if ($trace) { $env:COIN_RENDER_TRACE_PHASES = '1' }
  else { Remove-Item Env:COIN_RENDER_TRACE_PHASES -ErrorAction SilentlyContinue }
  $tag = "cube-replay-$flavor-$api-$count-$variant-$sample-$mode"
  $renderBackend = if ($flavor -eq 'coin') { 'gl' } else { $flavor }
  $arguments = @('--scene', "build/large-scenes/city-$count.iv", '--backend', $renderBackend,
    '--size', '1024', '--warmup', "$warmup", '--frames', "$frames")
  if ($mode -eq 'camera') { $arguments += '--dynamic' }
  if ($mode -eq 'material') { $arguments += '--material-dynamic' }
  $image = $null
  if ($sample -eq 1 -or $mode -ne 'static') {
    $image = "build/large-scenes/$tag.ppm"
    $arguments += @('--image-output', $image)
  }
  $watch = [Diagnostics.Stopwatch]::StartNew()
  & $exe @arguments *> "build/$tag.log"
  $exitCode = $LASTEXITCODE
  $watch.Stop()
  $script:runs += [pscustomobject]@{flavor=$flavor;api=$api;buildings=$count;variant=$variant;
    sample=$sample;tracing=$trace;warmup=$warmup;frames=$frames;mode=$mode;
    exit_code=$exitCode;process_wall_ms=$watch.Elapsed.TotalMilliseconds;log="build/$tag.log";image=$image}
  $script:runs | ConvertTo-Json -Depth 6 | Set-Content build/cube-replay-runs.json -Encoding UTF8
  Write-Output "$tag exit=$exitCode"
  Select-String -Path "build/$tag.log" -Pattern 'first_frame_ms=|PHASE action|PHASE plan_storage|rgba_fnv64='
  if ($exitCode -ne 0) { throw "$tag failed" }
}
for ($sample = 1; $sample -le 3; ++$sample) {
  Measure-CommonOptimization 'coin' 'opengl' 40000 'current' $sample $false 4 8
}
foreach ($flavor in @('wgpu','bgfx')) {
  $apis = if ($flavor -eq 'wgpu') { @('dx12','vulkan','gl') } else { @('d3d12','vulkan','opengl') }
  foreach ($api in $apis) {
    for ($sample = 1; $sample -le 3; ++$sample) {
      foreach ($variant in @('before','after')) {
        Measure-CommonOptimization $flavor $api 40000 $variant $sample
      }
    }
    foreach ($variant in @('before','after')) {
      Measure-CommonOptimization $flavor $api 40000 $variant 'trace' $true
      Measure-CommonOptimization $flavor $api 40000 $variant 'warm' $false 4 8
    }
  }
  foreach ($mode in @('camera','material')) {
    foreach ($variant in @('before','after')) {
      Measure-CommonOptimization $flavor 'vulkan' 10000 $variant 'update' $false 1 3 $mode
    }
  }
}
