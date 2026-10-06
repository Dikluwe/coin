$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
Remove-Item Env:COIN_RENDER_TRACE_PHASES -ErrorAction SilentlyContinue
Remove-Item Env:COIN_WGPU_CAMERA_BINDINGS -ErrorAction SilentlyContinue
Remove-Item Env:COIN_WGPU_DISABLE_OPAQUE_BATCHING -ErrorAction SilentlyContinue
$records = [System.Collections.Generic.List[object]]::new()
function Run-City($api, $version, $tag, $warmup, $frames, $update = 'static', $expectedExit = 0) {
  $env:WGPU_BACKEND = $api
  $exe = if ($version -eq 'before') { 'build/wgpu-large-baseline/coin_render_gl_benchmark.exe' } else { 'build/coin-render-wgpu-msvc/bin/coin_render_gl_benchmark.exe' }
  $log = "build/wgpu-final-$api-$version-$tag.log"
  $image = "build/large-scenes/wgpu-final-$api-$version-$tag.ppm"
  $argv = @('--scene','build/large-scenes/city-40000.iv','--backend','wgpu','--size','1024','--warmup',"$warmup",'--frames',"$frames",'--image-output',$image)
  if ($update -eq 'camera') { $argv += '--dynamic' }
  & $exe @argv *> $log
  $code = $LASTEXITCODE
  $records.Add([pscustomobject]@{api=$api; version=$version; tag=$tag; warmup=$warmup; frames=$frames; update=$update; trace=($env:COIN_RENDER_TRACE_PHASES -eq '1'); log=$log; image=$image; exit_code=$code})
  $records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath build/wgpu-large-measure-manifest.json -Encoding utf8
  if ($expectedExit -eq 0 -and $code -ne 0) { throw "$api $version $tag failed: $code" }
  if ($expectedExit -ne 0 -and $code -eq 0) { throw "$api expected baseline failure did not occur" }
  Write-Output "$api $version $tag exit=$code"
}
foreach ($api in @('vulkan','gl')) {
  foreach ($pair in 1..3) {
    Run-City $api before "pair$pair" 1 1
    Run-City $api after "pair$pair" 1 1
  }
}
Run-City dx12 before failure 1 1 static 1
foreach ($pair in 1..3) { Run-City dx12 after "pair$pair" 1 1 }
foreach ($api in @('dx12','vulkan','gl')) { Run-City $api after warm 4 8 }
Run-City vulkan before camera 2 3 camera
foreach ($api in @('dx12','vulkan','gl')) { Run-City $api after camera 2 3 camera }
$env:COIN_RENDER_TRACE_PHASES = '1'
foreach ($api in @('dx12','vulkan','gl')) { Run-City $api after trace 2 2 }
Remove-Item Env:COIN_RENDER_TRACE_PHASES
Write-Output 'All final large-scene measurements completed.'
