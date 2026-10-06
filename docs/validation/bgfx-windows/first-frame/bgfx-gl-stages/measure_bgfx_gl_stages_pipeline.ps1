$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_BGFX_RENDERER='opengl'
$env:COIN_BGFX_TRANSPARENCY='auto'
$env:COIN_RENDER_TRACE_PHASES='1'
Remove-Item Env:COIN_BGFX_DISABLE_COMPACT_VERTICES -ErrorAction SilentlyContinue
Remove-Item Env:COIN_BGFX_DISABLE_PROGRAM_CACHE -ErrorAction SilentlyContinue
$runs=@()
foreach ($depth in @(2,3)) {
  $env:COIN_BGFX_READBACK_PIPELINE_DEPTH="$depth"
  foreach ($mode in @('camera','material')) {
    foreach ($variant in @('before','after')) {
      $exe=if ($variant -eq 'before') { 'build/bgfx-gl-stages-baseline/coin_render_gl_benchmark.exe' } else { 'build/coin-render-bgfx-msvc/bin/coin_render_gl_benchmark.exe' }
      $update=if ($mode -eq 'camera') { '--dynamic' } else { '--material-dynamic' }
      $tag="bgfx-gl-stages-pipeline-$depth-$mode-$variant"
      $image="build/large-scenes/$tag.ppm"
      & $exe --scene build/large-scenes/city-10000.iv --backend bgfx --size 1024 --warmup 4 --frames 8 $update --image-output $image *> "build/$tag.log"
      $runs += [pscustomobject]@{api='opengl';mode=$mode;variant=$variant;depth=$depth;log="build/$tag.log";image=$image;exit_code=$LASTEXITCODE}
      if ($LASTEXITCODE -ne 0) { throw "$tag failed" }
      Write-Output "$tag passed"
    }
  }
}
$runs | ConvertTo-Json | Set-Content build/bgfx-gl-stages-pipeline-runs.json -Encoding UTF8
