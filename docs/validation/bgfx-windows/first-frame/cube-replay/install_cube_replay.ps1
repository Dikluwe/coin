$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$verification = @()
foreach ($flavor in @('wgpu', 'bgfx')) {
  $build = "build/coin-render-$flavor-msvc"
  $prefix = if ($flavor -eq 'wgpu') { 'build/coin-render-install' } else { 'build/coin-render-bgfx-install' }
  cmake --install $build --config Release *> "build/cube-replay-$flavor-install.log"
  if ($LASTEXITCODE -ne 0) { throw "$flavor installation failed" }
  foreach ($name in @('Coin4.dll', 'CoinRender4.dll', 'coin_render_gl_benchmark.exe')) {
    $built = (Get-FileHash "$build/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
    $installed = (Get-FileHash "$prefix/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($built -ne $installed) { throw "Installed $flavor $name differs from build" }
    $verification += [pscustomobject]@{flavor=$flavor;file=$name;build=$build;prefix=$prefix;sha256=$built;matches=$true}
  }
}
$verification | ConvertTo-Json -Depth 5 | Set-Content build/cube-replay-install-verification.json -Encoding UTF8
Write-Output 'Both local installations match their qualified Release builds.'
