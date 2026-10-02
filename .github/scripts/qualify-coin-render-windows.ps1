param(
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][ValidateSet('dx12', 'vulkan')][string]$Backend,
  [Parameter(Mandatory = $true)][string]$EvidenceDirectory,
  [Parameter(Mandatory = $true)][string]$ExpectedAdapterPattern
)

$ErrorActionPreference = 'Stop'
$buildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$evidencePath = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidencePath -Force | Out-Null
$savedEnvironment = @{}
foreach ($name in @('WGPU_BACKEND', 'COIN_RENDER_REQUIRE_GL_REFERENCE', 'COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU')) {
  $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
  $env:WGPU_BACKEND = $Backend
  $env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
  $env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
  Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption, Version, OSArchitecture | Format-List |
    Out-File -LiteralPath (Join-Path $evidencePath 'inventory.txt')
  Get-CimInstance Win32_VideoController |
    Select-Object Name, DriverVersion, PNPDeviceID | Format-List |
    Out-File -LiteralPath (Join-Path $evidencePath 'inventory.txt') -Append

  $capabilitiesFile = Join-Path $evidencePath 'capabilities.txt'
  & (Join-Path $buildPath 'bin/CoinRenderProductTest.exe') *> $capabilitiesFile
  if ($LASTEXITCODE -ne 0) { throw 'The physical adapter capability probe failed.' }
  $capabilities = Get-Content -LiteralPath $capabilitiesFile -Raw
  $renderer = if ($Backend -eq 'dx12') { 4 } else { 1 }
  if ($capabilities -notmatch " renderer=$renderer " -or
      $capabilities -notmatch $ExpectedAdapterPattern -or
      $capabilities -match 'Microsoft Basic|WARP|llvmpipe|lavapipe|SwiftShader') {
    throw 'The selected API/physical adapter does not match the requested cell.'
  }
  $glCapacityFile = Join-Path $evidencePath 'gl-capacity.txt'
  & (Join-Path $buildPath 'bin/CoinRenderShadowReferenceTest.exe') --gl-capacity *> $glCapacityFile
  if ($LASTEXITCODE -ne 0) { throw 'The Coin/GL capacity probe failed.' }

  $testsFile = Join-Path $evidencePath 'tests.txt'
  & ctest --test-dir $buildPath -C Release --output-on-failure -j 2 --output-junit `
    (Join-Path $evidencePath 'tests.xml') *> $testsFile
  $result = $LASTEXITCODE
  Get-Content -LiteralPath $testsFile -Tail 15
  if ($result -ne 0 -or (Get-Content -LiteralPath $testsFile -Raw) -match 'Skipped|Not Run') {
    throw 'The Windows cell has failures or skipped tests; inspect the evidence.'
  }
}
finally {
  foreach ($name in $savedEnvironment.Keys) {
    [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
  }
}
