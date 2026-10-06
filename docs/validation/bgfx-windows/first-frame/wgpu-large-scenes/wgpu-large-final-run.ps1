$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
& ./build/wgpu-large-measure.ps1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
Remove-Item Env:COIN_WGPU_CAMERA_BINDINGS -ErrorAction SilentlyContinue
$env:WGPU_BACKEND = 'vulkan'
& ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --output-junit H:/Git/coin/build/wgpu-large-final-vulkan-tests.xml *> build/wgpu-large-final-vulkan-tests.log
if ($LASTEXITCODE -ne 0) { throw "Final Vulkan suite failed: $LASTEXITCODE" }
Write-Output 'Final Vulkan suite passed: 100 tests.'
$env:WGPU_BACKEND = 'dx12'
$env:COIN_WGPU_CAMERA_BINDINGS = '1'
& ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/wgpu-large-cross-api-tests.txt --output-junit H:/Git/coin/build/wgpu-large-final-dx12-tests.xml *> build/wgpu-large-final-dx12-tests.log
if ($LASTEXITCODE -ne 0) { throw "Final D3D12 suite failed: $LASTEXITCODE" }
Write-Output 'Final D3D12 suite passed: 29 tests.'
& 'C:/Users/Diklu/.cargo/bin/cargo.exe' test --manifest-path build/coin-render-source/src/rendering/coinwgpu/rust_bridge/Cargo.toml --release --offline --target-dir build/coin-render-wgpu-msvc/rust_target *> build/wgpu-large-final-rust-tests.log
if ($LASTEXITCODE -ne 0) { throw "Final Rust tests failed: $LASTEXITCODE" }
& cmake --install build/coin-render-wgpu-msvc --config Release *> build/wgpu-large-install.log
if ($LASTEXITCODE -ne 0) { throw "Local install failed: $LASTEXITCODE" }
Write-Output 'Final Rust tests and local install passed.'
