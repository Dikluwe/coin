$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_BGFX_RENDERER = 'opengl'
$env:COIN_BGFX_TRANSPARENCY = 'auto'
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_BGFX_DISABLE_COMPACT_VERTICES','COIN_BGFX_DISABLE_PROGRAM_CACHE','COIN_BGFX_READBACK_PIPELINE_DEPTH','COIN_BGFX_DISABLE_DRAW_BATCHING')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
$runs = @()
for ($sample=1; $sample -le 3; ++$sample) {
  foreach ($mode in @('compact-cache-off','full-layout')) {
    Remove-Item Env:COIN_BGFX_DISABLE_COMPACT_VERTICES -ErrorAction SilentlyContinue
    Remove-Item Env:COIN_BGFX_DISABLE_PROGRAM_CACHE -ErrorAction SilentlyContinue
    if ($mode -eq 'compact-cache-off') { $env:COIN_BGFX_DISABLE_PROGRAM_CACHE='1' }
    else { $env:COIN_BGFX_DISABLE_COMPACT_VERTICES='1' }
    $tag="bgfx-gl-stages-ablation-$mode-$sample"
    $image="build/large-scenes/$tag.ppm"
    & build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe --scene build/large-scenes/city-40000.iv --backend bgfx --size 1024 --warmup 1 --frames 3 --image-output $image *> "build/$tag.log"
    $runs += [pscustomobject]@{mode=$mode;sample=$sample;log="build/$tag.log";image=$image;exit_code=$LASTEXITCODE}
    if ($LASTEXITCODE -ne 0) { throw "$tag failed" }
    Select-String "build/$tag.log" -Pattern 'first_frame_ms='
  }
}
$runs | ConvertTo-Json | Set-Content build/bgfx-gl-stages-ablation-runs.json -Encoding UTF8
