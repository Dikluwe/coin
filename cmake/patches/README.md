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


## Vulkan explicit sample interpolation

`bgfx-vulkan-sample-interpolation.patch` is a separate, opt-in dependency patch
for BGFX `81d81fba72c42d348c589514c774bbfe01e110fa` (API 161). Its paths start
at the BGFX source root. It changes only `include/bgfx/defines.h` and
`src/renderer_vk.cpp`; it does not modify Coin's public ABI or enable global
sample shading on pipelines.

The patch negotiates `VkPhysicalDeviceFeatures::sampleRateShading` only when
both the physical device supports it and `Init::capabilities` requests the
private capability. `Caps::supported` advertises the capability only when the
feature was enabled. This enables explicit sample interpolation instructions;
`VkPipelineMultisampleStateCreateInfo::sampleShadingEnable` stays false.
Clearing the capability in `Init::capabilities` disables negotiation and its
advertisement. Ordinary BGFX initialization requests all capabilities by default.

The local extension is named `COIN_BGFX_SAMPLE_INTERPOLATION_EXTENSION_VERSION`
(version 1), with `COIN_BGFX_CAPS_SAMPLE_INTERPOLATION` at bit 62. This is a
private reservation in the pinned dependency, **not an upstream bit allocation**.
The header checks API 161 and collisions with every upstream capability in that
revision, and records the source revision. Audit the complete capability list
and reassign/version the extension before rebasing or regenerating the IDL
headers. Do not copy these macros into stock SDK headers to claim support.
CoinRender requires both the recognized header extension and the enabled runtime
bit; a stock SDK, unsupported device, or disabled capability still rejects MSAA
Vulkan peeling before scene encoding. Other renderers keep their existing path.

Build and install into a distinct prefix, then rebuild CoinRender against that
prefix's matching headers and library. Do not overwrite a shared SDK. A complete
Linux dependency build can be prepared with these pinned revisions:

| Repository | Revision |
| --- | --- |
| bgfx.cmake | `a952acef35b431a39c790d39736867c515f32aff` |
| bgfx | `81d81fba72c42d348c589514c774bbfe01e110fa` |
| bx | `25315498841259323e18f549d1ad9d9aba6632cc` |
| bimg | `87aaad3ac882e741889fdd4263224e5d12c26f99` |

```sh
coin_source="$PWD"
deps=$(mktemp -d /dev/shm/coinrender-bgfx-sample-interpolation.XXXXXX)
git clone --no-checkout https://github.com/bkaradzic/bgfx.cmake.git "$deps/source"
git -C "$deps/source" checkout a952acef35b431a39c790d39736867c515f32aff
git -C "$deps/source" submodule update --init --recursive
git -C "$deps/source/bgfx" checkout 81d81fba72c42d348c589514c774bbfe01e110fa
git -C "$deps/source/bx" checkout 25315498841259323e18f549d1ad9d9aba6632cc
git -C "$deps/source/bimg" checkout 87aaad3ac882e741889fdd4263224e5d12c26f99
git -C "$deps/source/bgfx" apply --check "$coin_source/cmake/patches/bgfx-vulkan-sample-interpolation.patch"
git -C "$deps/source/bgfx" apply "$coin_source/cmake/patches/bgfx-vulkan-sample-interpolation.patch"
cmake -S "$deps/source" -B "$deps/build" -DBGFX_BUILD_EXAMPLES=OFF \
  -DBGFX_BUILD_TESTS=OFF -DBGFX_BUILD_TOOLS=ON -DBGFX_INSTALL=ON \
  -DBGFX_LIBRARY_TYPE=STATIC -DCMAKE_INSTALL_PREFIX="$deps/prefix"
cmake --build "$deps/build" --parallel 2
cmake --install "$deps/build"
```

Check each command's exit status before proceeding. Preserve the original SDK's
configuration and other intentionally applied platform patches when comparing
builds. For a constrained incremental overlay, only `renderer_vk.cpp` needs a
new object: use the original compiler flags and patched header, replace that
member in a **copy** of the original archive, and expose the matching patched
header through the separate prefix. If the overlay injects `defines.h` with
`-include`, inject `stdint.h` first so `UINT32_C` and `UINT64_C` are defined
before the extension's preprocessor checks. Reused objects/libraries remain read-only.
Any such overlay must use the pinned source and the same compiler configuration;
a header-only change cannot enable the Vulkan feature. Build tools may regenerate
headers, so verify that the extension remains present in the installed header.

The patch alone is not GPU qualification. Verify the runtime capability with
negotiation enabled and disabled, and validate per-sample peeling, publication,
resource recovery and the unaffected profiles against the chosen SDK build.


## Vendored wgpu-hal GLES device state

The vendored wgpu-hal 24.0.4 queue restores its owning device's VAO at the start
of command-buffer execution, before resetting ELEMENT_ARRAY_BUFFER. Several
logical devices share the adapter's GLES context. Binding a VAO only when opening
a device leaves a different device's VAO active; destroying that device removes
the active binding and breaks subsequent draws from a surviving device.

The queue borrows the handle; Device remains the sole owner and deletes it.
wgpu-core keeps Device alive until its queue has been dropped. This changes no
capability bits, creates no extra context and keeps no destroyed peer alive.
The Rust bridge build tracks the modified GLES mod.rs, adapter.rs and queue.rs.
The raw ABI61 cubemap regression includes creation, rendering and destruction of
an independent device before consuming the surviving device's resource; its
original ordering must be preserved. B72 in the feature matrix records the
bounded GPU qualification and remaining regressions.

## BGFX completed-frame flags and presentation

`bgfx-frame-flush-before-signal.patch` targets BGFX
`81d81fba72c42d348c589514c774bbfe01e110fa`. It snapshots the completed frame's
flush flag before `renderSemPost()` releases the API thread. That thread can
swap `m_render` before the render thread reads the flag used by
`flipAfterRender` and `m_flushPrevFrame`. A normal scene followed immediately
by a resource-only `BGFX_FRAME_FLUSH` can therefore suppress presentation of
the scene while returning its correct screenshot. The patch keeps the flag
of the completed frame; it adds no frame, replay, wait, context or vendor path.

Apply manually to a separate BGFX source checkout, alongside the other required
patches, then rebuild the SDK and relink CoinRender. Paths start at the BGFX
source root. Coin CMake does not apply this dependency patch automatically.
The local source/build used for the comparison match the installed
`bgfx.cpp.o`; preserve its Release configuration when replacing that object.
Exported Debug consumer definitions do not identify the configuration of an
existing SDK archive.

A temporary atomic scheduling experiment reproduced the normal-to-flush handoff
on NVIDIA/Vulkan: the old SDK returned the current screenshot but retained a
black physical window through the original five-second deadline; the patch
passed the same input, publication counts, color bounds, invalid-input refusal
and recovery. Scheduling hooks are not part of this patch or the product.
`CoinRenderWindowTransitionTest` retains a short public-scene regression for
matrix and projective-Q transitions. The same scheduling experiment failed
with the old SDK and passed with the patch on AMD/NVIDIA Vulkan/OpenGL (four
cells). Seven normal controls and 40 affected regression processes passed.
These cover the original MSAA window campaign, cube RTT, mips, nested producers,
tickets and late failures. The local build uses a separate patched SDK prefix.
This correction does not close the NVIDIA EGL/XCB teardown issue B34.
