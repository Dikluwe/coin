param([string]$RunSet = 'all')
$ErrorActionPreference = 'Stop'
$runs = @()
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES',
  'COIN_WGPU_GPU_TIMESTAMPS','COIN_WGPU_CAMERA_BINDINGS',
  'COIN_WGPU_DISABLE_OPAQUE_BATCHING','COIN_BGFX_DISABLE_DRAW_BATCHING',
  'COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
$env:COIN_BGFX_TRANSPARENCY = 'auto'
function Measure-CommonDetail($flavor, $api, $count, $side, $variant, $sample, $trace = $false) {
  $exe = if ($variant -eq 'baseline') {
    "build/first-frame-details-baseline/$flavor/coin_render_gl_benchmark.exe"
  } else { "build/coin-render-$flavor-msvc/bin/coin_render_gl_benchmark.exe" }
  $env:WGPU_BACKEND = $api
  $env:COIN_BGFX_RENDERER = $api
  if ($trace) { $env:COIN_RENDER_TRACE_PHASES = '1' }
  else { Remove-Item Env:COIN_RENDER_TRACE_PHASES -ErrorAction SilentlyContinue }
  $tag = "common-details-$flavor-$api-$count-$side-$variant-$sample"
  $argsList = @('--scene', "build/large-scenes/city-$count.iv", '--backend', $flavor,
    '--size', "$side", '--warmup', '1', '--frames', '3')
  $image = $null
  if ($sample -eq 1 -and $count -eq 40000 -and $side -eq 1024) {
    $image = "build/large-scenes/$tag.ppm"
    $argsList += @('--image-output', $image)
  }
  $watch = [Diagnostics.Stopwatch]::StartNew()
  & $exe @argsList *> "build/$tag.log"
  $exitCode = $LASTEXITCODE
  $watch.Stop()
  $script:runs += [pscustomobject]@{flavor=$flavor;api=$api;buildings=$count;side=$side;
    variant=$variant;sample=$sample;tracing=$trace;exit_code=$exitCode;
    process_wall_ms=$watch.Elapsed.TotalMilliseconds;log="build/$tag.log";image=$image}
  $script:runs | ConvertTo-Json -Depth 6 | Set-Content build/common-details-runs.json -Encoding UTF8
  Write-Output "$tag exit=$exitCode"
  Select-String -Path "build/$tag.log" -Pattern 'first_frame_ms=|PHASE action|PHASE builder_detail|PHASE plan_storage'
  if ($exitCode -ne 0) { throw "$tag failed" }
}
foreach ($flavor in @('wgpu','bgfx')) {
  $apis = if ($flavor -eq 'wgpu') { @('dx12','vulkan','gl') } else { @('d3d12','vulkan','opengl') }
  foreach ($api in $apis) {
    for ($sample = 1; $sample -le 3; ++$sample) {
      foreach ($variant in @('baseline','instrumented')) {
        Measure-CommonDetail $flavor $api 40000 1024 $variant $sample
      }
    }
    Measure-CommonDetail $flavor $api 40000 1024 'instrumented' 'trace' $true
  }
  foreach ($count in @(100,10000)) {
    Measure-CommonDetail $flavor 'vulkan' $count 1024 'instrumented' 'scale' $true
  }
  Measure-CommonDetail $flavor 'vulkan' 40000 256 'instrumented' 'resolution' $true
}
