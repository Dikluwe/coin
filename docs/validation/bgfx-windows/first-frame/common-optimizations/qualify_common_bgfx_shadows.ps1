$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$xml = [xml](Get-Content build/common-opt-bgfx-tests.xml -Raw)
$skipped = @($xml.testsuite.testcase | Where-Object { $_.skipped -ne $null } | ForEach-Object { $_.name })
if ($skipped.Count -ne 32) { throw 'Expected exactly 32 explicitly gated shadow cases' }
$skipped | Set-Content build/common-opt-bgfx-shadow-tests.txt -Encoding ascii
$env:COIN_RENDER_REQUIRE_BGFX_SHADOW_GPU = '1'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_BGFX_RENDERER = 'vulkan'
Remove-Item Env:COIN_RENDER_TRACE_PHASES -ErrorAction SilentlyContinue
ctest --test-dir build/coin-render-bgfx-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/common-opt-bgfx-shadow-tests.txt --output-junit H:/Git/coin/build/common-opt-bgfx-shadow-tests.xml *> build/common-opt-bgfx-shadow-tests.log
if ($LASTEXITCODE -ne 0) { throw 'BGFX mandatory GPU shadow cases failed' }
Write-Output 'BGFX mandatory GPU shadow cases passed.'
