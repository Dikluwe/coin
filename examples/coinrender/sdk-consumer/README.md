# Installed CoinRender SDK consumer

This standalone CMake project uses only public imported targets/headers and system
OpenGL. Install Coin/CoinRender with `COIN_INSTALL_RENDER_EXPERIMENTAL=ON` and the
legacy renderer enabled for the CoinGL reference. Shared BGFX SDK consumers do
not require BGFX headers/package; Windows retains NOMINMAX.

```sh
cmake -S examples/coinrender/sdk-consumer -B build/sdk-consumer -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build/sdk-consumer --config Release
```

Run `coin-render-sdk-consumer vulkan`, `opengl` or `d3d12` with the installed
configuration/library path. The API is requested explicitly; unavailable API
returns diagnostic/exit 3, without counting another API as a pass. The client queries
capabilities, renders a red BASE_COLOR cube in 32×32, checks center texel and compares
all orientation-normalized RGB pixels against CoinGL (maximum 3 levels). It prints
API/adapter, GL coordinate/sampler limits and results. This is an offscreen SDK
smoke, not every feature/native surface/eight-map qualification.

Select/record physical GPU in the runner. Functional success alone does not prove
hardware: reject software adapters for qualification and record executable/library/
header/prefix hashes. Never mix builds/revisions. Missing reference/API cannot be
counted as visual equivalence.

## Sampling and C11 API

Optional second argument `native` (default) or `portable`; optional third is output
PPM prefix:

```sh
coin-render-sdk-consumer d3d12 portable artifacts/sdk-d3d12-portable
coin-render-sdk-capabilities-c
```

The client also minifies a 128×128 image into 64×64. Portable requires independent
NEAREST_MIPMAP_LINEAR oracle channels 40/160 in columns 30/31, maximum error 1.
Native requires complete rendered pixels without imposing portable expected
sampling. `*-cube.ppm`/`*-mip.ppm` retain actual output.

Availability is queried without requiring published qualification; strict selection
is logged separately. Historical Windows strict query remains UNQUALIFIED_PROFILE.
Passing an identified-device smoke does not publish a general Windows profile.
Windows prints loaded Coin4.dll/CoinRender4.dll paths; compare installation hashes.
C11 probe checks capability V4 size 584, V3 prefix 536 and native ordinal 0.

## Windows reproduction recipe

Use Visual Studio 2022 C/C++ tools, Windows SDK, CMake/Git and physical-GPU driver
for requested API. RUST_BRIDGE requires x64 MSVC Rust/Cargo and preserved Cargo.lock.
For BGFX prepare [pinned dependency patches](../../../cmake/patches/README.md).
Use separate BGFX/wgpu install prefixes. This standalone C++ client requires
C++17 and its C probe C11; Coin public headers retain C++11 compatibility.

PowerShell from checkout root after preparing BGFX:

```powershell
$coinSource = (Get-Location).Path
$bgfxSdk = Join-Path $coinSource 'build/coinrender-windows-deps/install'
$coinSdk = Join-Path $coinSource 'build/sdk-bgfx'
cmake -S . -B build/windows-bgfx -G 'Visual Studio 17 2022' -A x64 `
  -DCOIN_BUILD_SHARED_LIBS=ON -DCOIN_BUILD_TESTS=ON `
  -DCOIN_BUILD_LEGACY_GL_RENDERER=ON -DCOIN_BUILD_RENDER=ON `
  -DCOIN_RENDER_BACKEND=BGFX -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON `
  "-DCMAKE_PREFIX_PATH=$bgfxSdk" "-DCMAKE_INSTALL_PREFIX=$coinSdk" `
  "-DCOIN_BGFX_SHADERC_EXECUTABLE=$bgfxSdk/bin/shaderc.exe" `
  "-DCOIN_BGFX_SHADER_INCLUDE_DIR=$bgfxSdk/include/bgfx"
cmake --build build/windows-bgfx --config Release --parallel 2
ctest --test-dir build/windows-bgfx -C Release --output-on-failure -j 1
cmake --install build/windows-bgfx --config Release
cmake -S examples/coinrender/sdk-consumer -B build/consumer-bgfx `
  -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$coinSdk"
cmake --build build/consumer-bgfx --config Release --parallel 2
$env:PATH = "$coinSdk/bin;$env:PATH"
& ./build/consumer-bgfx/Release/coin-render-sdk-capabilities-c.exe
& ./build/consumer-bgfx/Release/coin-render-sdk-consumer.exe d3d12 portable
```

Check each command's exit status. Repeat physical APIs vulkan/opengl with both
native/portable sampling. For wgpu use another build/install prefix and
COIN_RENDER_BACKEND=RUST_BRIDGE, omitting the three BGFX-specific options, then
build/test/install/consume only that prefix. Run heavy builds separately and GPU
tests one process at a time. These are recipes, not new Windows qualification.

Historical Linux extraction built RECORDING/RUST_BRIDGE/BGFX/OFF and installed
C/C++ clients; Vulkan portable smokes passed. Those runs do not qualify MSVC,
DLL import/export or current Windows consumers. Record actual Windows results and
loaded DLL hashes. The original extraction receipt and limitations are available
through [evidence packaging](../../../docs/coin-render-evidence.md), rather than
assuming current source layout is a new validation report. Known historical
BGFX/OpenGL/NVIDIA pixel and Rust parallel-shutdown differences remain diagnosed
within their own dated profiles; serial success never proves a parallel fix.
Recovered BGFX patch bytes do not themselves fix GPU/driver failures. PBR is
outside this contribution.
