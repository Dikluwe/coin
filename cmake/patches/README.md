# CoinRender dependency patches

These patches modify external BGFX sources, not Coin's public API. Apply them
only to a separate dependency checkout. Coin's CMake configuration does not
apply them automatically.

## Windows D3D12 recovery and query history

`bgfx-device-loss.patch` preserves the final 2,459-byte patch published in
[Dikluwe/coin commit 68657a5b969f252234760080be35acf67350b40d](https://github.com/Dikluwe/coin/commit/68657a5b969f252234760080be35acf67350b40d).
Its SHA256 is `4a0d2b7785f9369a789a4a88ac5171db142ca5955a8e7b2d776325b0a4e26c79`.
It releases secondary D3D12 swapchains during shutdown after device removal,
and adds opt-in query history through `COIN_BGFX_D3D12_QUERY_LOG`. The history
file opens in `TimerQueryD3D12::create()` so tracing survives resize recreation.
Leave that environment variable unset during performance measurements unless
the measurement explicitly concerns query tracing.

The pinned dependency revisions are:

| Repository | Revision |
| --- | --- |
| bgfx.cmake | `eb88f6be55ae223966698c802542c24b0d93fd25` |
| bgfx | `7346c3e731bd65f35c7e6a99819840e554f5b748` |
| bx | `1c986bd1e9a176a08ae885a6cdcefe76c3f700fc` |
| bimg | `6b08e87de28e7aa54782d5ce1b279dca373a10c6` |

The original revision list and recovery receipt are preserved verbatim in
`bgfx-device-loss-provenance.json` and
`bgfx-device-loss-recovery-20261015.json`. The earlier `before-history` patch
does not replace this final patch.

Example PowerShell commands, starting in the clean Coin checkout, with Git,
CMake and Visual Studio 2022 C++ tools available:

```powershell
$coinSource = (Get-Location).Path
$patch = Join-Path $coinSource 'cmake/patches/bgfx-device-loss.patch'
$deps = Join-Path $coinSource 'build/coinrender-windows-deps'
$sdk = Join-Path $deps 'install'
New-Item -ItemType Directory -Force $deps | Out-Null
git clone --no-checkout https://github.com/bkaradzic/bgfx.cmake.git "$deps/source"
git -C "$deps/source" checkout eb88f6be55ae223966698c802542c24b0d93fd25
git -C "$deps/source" submodule update --init --recursive
git -C "$deps/source/bgfx" checkout 7346c3e731bd65f35c7e6a99819840e554f5b748
git -C "$deps/source/bx" checkout 1c986bd1e9a176a08ae885a6cdcefe76c3f700fc
git -C "$deps/source/bimg" checkout 6b08e87de28e7aa54782d5ce1b279dca373a10c6
git -C "$deps/source" apply --check $patch
git -C "$deps/source" apply $patch
cmake -S "$deps/source" -B "$deps/build" -G 'Visual Studio 17 2022' -A x64 `
  -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TESTS=OFF -DBGFX_BUILD_TOOLS=ON `
  -DBGFX_INSTALL=ON -DBGFX_LIBRARY_TYPE=STATIC -DBGFX_CONFIG_MAX_DRAW_CALLS=131072 `
  "-DCMAKE_INSTALL_PREFIX=$sdk"
cmake --build "$deps/build" --config Release --parallel 2
cmake --install "$deps/build" --config Release
cmake -S $coinSource -B build/coinrender-windows -G 'Visual Studio 17 2022' -A x64 `
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=BGFX `
  -DCOIN_INSTALL_RENDER_EXPERIMENTAL=ON "-DCMAKE_PREFIX_PATH=$sdk" `
  "-DCOIN_BGFX_SHADERC_EXECUTABLE=$sdk/bin/shaderc.exe" `
  "-DCOIN_BGFX_SHADER_INCLUDE_DIR=$sdk/include/bgfx"
cmake --build build/coinrender-windows --config Release --parallel 2
```

Check each command's exit status before proceeding. Run heavy builds one at a
time. These are portable instructions derived from the recorded Windows build;
they have not been executed as a new Windows qualification of the clean branch.

On 2026-10-15 the clean-branch check downloaded the two source files from the
exact BGFX revision, passed `git apply --check`, applied the patch and matched
both raw LF source hashes from the recovered Windows SDK receipt. Its results
are in `bgfx-device-loss-clean-verification-20261015.json`. The recovery receipt's
other applied hashes use CRLF fixtures; this line-ending distinction is explicit.
No SDK compilation or GPU gate was run for this incorporation. Existing visual
failures, BGFX/OpenGL/NVIDIA instancing discrepancy and parallel Rust teardown
failure remain open. Historical campaign evidence is not bundled here.

## Linux EGL swap interval

`bgfx-egl-swap-interval-before-prime.patch` is the existing Linux EGL adjustment.
Its paths start at BGFX's own source root (`src/`), while the Windows patch paths
start at the bgfx.cmake checkout (`bgfx/src/`). Do not treat the two patches as
interchangeable or assume the Windows dependency revisions qualify the EGL patch.
