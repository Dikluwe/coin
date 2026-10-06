$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
foreach ($option in @('COIN_RENDER_TRACE_PHASES','COIN_BGFX_DISABLE_COMPACT_VERTICES','COIN_BGFX_DISABLE_PROGRAM_CACHE','COIN_BGFX_READBACK_PIPELINE_DEPTH')) {
  Remove-Item "Env:$option" -ErrorAction SilentlyContinue
}
foreach ($api in @('vulkan','d3d12')) {
  $env:COIN_BGFX_RENDERER = $api
  ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure -R "^CoinBgfxOffscreenTest$|^CoinBgfxReadbackModes_$api`$" --output-junit "H:/Git/coin/build/bgfx-gl-stages-lifetime-$api.xml" *> "build/bgfx-gl-stages-lifetime-$api.log"
  Get-Content "build/bgfx-gl-stages-lifetime-$api.log" -Tail 6
  if ($LASTEXITCODE -ne 0) { throw "$api regression failed" }
}
