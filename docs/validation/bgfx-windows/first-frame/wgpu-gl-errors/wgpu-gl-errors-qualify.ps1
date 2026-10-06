$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath 'H:/Git/coin'
$env:COIN_RENDER_REQUIRE_GL_REFERENCE = '1'
$env:COIN_WGPU_REQUIRE_GL_REFERENCE = '1'
$env:COIN_RENDER_REQUIRE_WGPU_SHADOW_GPU = '1'
Remove-Item Env:COIN_WGPU_CAMERA_BINDINGS -ErrorAction SilentlyContinue
& cmake --build build/coin-render-wgpu-msvc --config Release --parallel 2 *> build/wgpu-gl-errors-final-build.log
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$env:WGPU_BACKEND = 'gl'
& 'C:/Users/Diklu/.cargo/bin/cargo.exe' test --manifest-path build/coin-render-source/src/rendering/coinwgpu/rust_bridge/Cargo.toml --release --offline --target-dir build/coin-render-wgpu-msvc/rust_target -- --test-threads=1 --nocapture *> build/wgpu-gl-errors-final-rust-tests.log
if ($LASTEXITCODE -ne 0) { throw 'Rust/GL depth conversion failed' }
Write-Output 'Rust tests and exact GL depth conversion passed.'
& ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure -R '^(CoinRenderPeelingTest|CoinRenderTransparencyTest)$' *> build/wgpu-gl-errors-peeling-tests.log
if ($LASTEXITCODE -ne 0) { throw 'GL peeling/transparency failed' }
Write-Output 'GL peeling and transparency passed.'
& ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --output-junit H:/Git/coin/build/wgpu-gl-errors-final-gl-tests.xml *> build/wgpu-gl-errors-final-gl-tests.log
if ($LASTEXITCODE -ne 0) { throw 'Full GL suite failed' }
Write-Output 'Full GL suite passed.'
$env:COIN_WGPU_CAMERA_BINDINGS = '1'
foreach ($api in @('vulkan', 'dx12')) {
  $env:WGPU_BACKEND = $api
  & ctest --test-dir build/coin-render-wgpu-msvc -C Release --parallel 1 --output-on-failure --tests-from-file H:/Git/coin/build/wgpu-large-cross-api-tests.txt --output-junit "H:/Git/coin/build/wgpu-gl-errors-final-$api-tests.xml" *> "build/wgpu-gl-errors-final-$api-tests.log"
  if ($LASTEXITCODE -ne 0) { throw "$api regression suite failed" }
  & 'C:/Users/Diklu/.cargo/bin/cargo.exe' test --manifest-path build/coin-render-source/src/rendering/coinwgpu/rust_bridge/Cargo.toml --release --offline --target-dir build/coin-render-wgpu-msvc/rust_target conversion_preserves_depth_bits_and_padded_rows -- --nocapture *> "build/wgpu-gl-errors-depth-$api-tests.log"
  if ($LASTEXITCODE -ne 0) { throw "$api exact depth conversion failed" }
  Write-Output "$api regression and exact depth conversion passed."
}
Remove-Item Env:COIN_WGPU_CAMERA_BINDINGS
foreach ($api in @('gl', 'vulkan', 'dx12')) {
  $env:WGPU_BACKEND = $api
  & build/coin-render-wgpu-msvc/bin/coin_render_gl_benchmark.exe --scene build/large-scenes/city-40000.iv --backend wgpu --size 1024 --warmup 4 --frames 8 --image-output "build/large-scenes/wgpu-gl-errors-$api-city.ppm" *> "build/wgpu-gl-errors-$api-city.log"
  if ($LASTEXITCODE -ne 0) { throw "$api city control failed" }
  Write-Output "$api city control passed."
}
& cmake --install build/coin-render-wgpu-msvc --config Release *> build/wgpu-gl-errors-install.log
if ($LASTEXITCODE -ne 0) { throw 'Local installation failed' }
& 'C:/Users/Diklu/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' build/summarize_wgpu_gl_errors.py
if ($LASTEXITCODE -ne 0) { throw 'Evidence/image verification failed' }
