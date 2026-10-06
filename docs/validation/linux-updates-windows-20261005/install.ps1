$ErrorActionPreference='Stop'
$taskRoot='H:/Git/coin/build/linux-updates-windows-20261005'
$taskInstallEvidence=@{}
foreach ($backend in @('bgfx','wgpu')) {
  $build="H:/Git/coin/build/coin-render-$backend-msvc"
  $prefix=if ($backend -eq 'bgfx') { 'H:/Git/coin/build/coin-render-bgfx-install' } else { 'H:/Git/coin/build/coin-render-install' }
  cmake --install $build --config Release *> "$taskRoot/install-$backend.log"
  if ($LASTEXITCODE -ne 0) { throw "Install failed: $backend" }
  Copy-Item -LiteralPath "$build/bin/coin_render_gl_benchmark.exe" -Destination "$prefix/bin/coin_render_gl_benchmark.exe"
  $taskHashes=@{}
  foreach ($name in @('Coin4.dll','CoinRender4.dll','coin_render_gl_benchmark.exe')) {
    $builtHash=(Get-FileHash -LiteralPath "$build/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
    $installedHash=(Get-FileHash -LiteralPath "$prefix/bin/$name" -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($builtHash -ne $installedHash) { throw "Installed binary mismatch: $backend/$name" }
    $taskHashes[$name]=$installedHash
  }
  $taskInstallEvidence[$backend]=@{prefix=$prefix;binary_sha256=$taskHashes}
}
$taskInstallEvidence | ConvertTo-Json -Depth 6 | Set-Content "$taskRoot/installed-hashes.json"
Write-Output 'Both local installations match qualified build binaries.'
