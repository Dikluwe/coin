$ErrorActionPreference='Stop'
$taskRoot='H:/Git/coin/build/linux-updates-windows-20261005'
foreach ($backend in @('bgfx','wgpu')) {
  cmake --build "H:/Git/coin/build/coin-render-$backend-msvc" --config Release --parallel 4 *> "$taskRoot/build-final-$backend.log"
  if ($LASTEXITCODE -ne 0) { Get-Content "$taskRoot/build-final-$backend.log" -Tail 20; throw "Build failed: $backend" }
}
$env:WGPU_BACKEND='gl'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE='1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE='1'
ctest --test-dir H:/Git/coin/build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure -R '^CoinRenderDrawStyleTest$' --output-junit "$taskRoot/tests-draw-style-gl.xml" *> "$taskRoot/tests-draw-style-gl.log"
Get-Content "$taskRoot/tests-draw-style-gl.log" -Tail 12
if ($LASTEXITCODE -ne 0) { throw 'DrawStyle GL CTest failed' }
& "$taskRoot/camera_reference.ps1"
if ($LASTEXITCODE -ne 0) { throw 'Camera reference gates failed' }
Write-Output 'All required follow-up gates passed.'
