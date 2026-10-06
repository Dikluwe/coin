$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_BGFX_TRANSPARENCY = 'auto'
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_WGPU_TRACE_PHASES','COIN_BGFX_DISABLE_DRAW_BATCHING',
  'COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_BGFX_DIAGNOSTIC_CPU_DEPTH_FILL','COIN_WGPU_GPU_TIMESTAMPS')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
$runs = @()
function Measure-Indexed($api, $variant, $sample, $trace = $false, $warmup = 1, $frames = 3, $mode = 'static') {
  $exe = if ($variant -eq 'before') { 'build/bgfx-gl-upload-baseline/coin_render_gl_benchmark.exe' }
    else { 'build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe' }
  $env:COIN_BGFX_RENDERER = $api
  if ($trace) { $env:COIN_RENDER_TRACE_PHASES = '1' }
  else { Remove-Item Env:COIN_RENDER_TRACE_PHASES -ErrorAction SilentlyContinue }
  $tag = "bgfx-gl-upload-$api-$variant-$sample-$mode"
  $count = if ($mode -eq 'static') { 40000 } else { 10000 }
  $arguments = @('--scene', "build/large-scenes/city-$count.iv", '--backend','bgfx',
    '--size','1024','--warmup',"$warmup",'--frames',"$frames")
  if ($mode -eq 'camera') { $arguments += '--dynamic' }
  if ($mode -eq 'material') { $arguments += '--material-dynamic' }
  $image = "build/large-scenes/$tag.ppm"
  $arguments += @('--image-output',$image)
  $ErrorActionPreference = 'Continue'
  & $exe @arguments *> "build/$tag.log"
  $ErrorActionPreference = 'Stop'
  $exitCode = $LASTEXITCODE
  $script:runs += [pscustomobject]@{api=$api;variant=$variant;sample=$sample;tracing=$trace;
    warmup=$warmup;frames=$frames;mode=$mode;exit_code=$exitCode;log="build/$tag.log";image=$image}
  $script:runs | ConvertTo-Json -Depth 5 | Set-Content build/bgfx-gl-upload-runs.json -Encoding UTF8
  Write-Output "$tag exit=$exitCode"
  Select-String -Path "build/$tag.log" -Pattern 'first_frame_ms=|PHASE bgfx lower|rgba_fnv64='
  if ($exitCode -ne 0) { throw "$tag failed" }
}
foreach ($api in @('opengl','vulkan','d3d12')) {
  for ($sample = 1; $sample -le 3; ++$sample) {
    foreach ($variant in @('before','after')) { Measure-Indexed $api $variant $sample }
  }
  foreach ($variant in @('before','after')) {
    Measure-Indexed $api $variant 'trace' $true
    Measure-Indexed $api $variant 'warm' $false 4 8
  }
}
foreach ($mode in @('camera','material')) {
  foreach ($variant in @('before','after')) {
    Measure-Indexed 'opengl' $variant 'update' $false 1 3 $mode
  }
}
