$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
cmake --install build/coin-render-bgfx-msvc --config Release *> build/bgfx-gl-indexed-install.log
if ($LASTEXITCODE -ne 0) { throw 'BGFX installation failed' }
$verification = @()
foreach ($name in @('Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe')) {
  $built = (Get-FileHash "build/coin-render-bgfx-msvc/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
  $installed = (Get-FileHash "build/coin-render-bgfx-install/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($built -ne $installed) { throw "Installed BGFX $name differs from build" }
  $verification += [pscustomobject]@{file=$name;sha256=$built;matches=$true}
}
$verification | ConvertTo-Json | Set-Content build/bgfx-gl-indexed-install-verification.json -Encoding UTF8
Write-Output 'Installed BGFX files match the qualified Release build.'
