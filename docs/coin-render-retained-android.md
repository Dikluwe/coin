> Follow-up: the retained capture is now connected to the million-city Android
> window viewer. See [the controlled same-APK comparison](coin-render-million-retained-android.md).
> The measurements and scope below describe the earlier experiment.

# Android emulated retained connector — 2026-10-27

The current working tree on `codex/coinrender-retained-integration` was built for
Android x86_64 and executed inside an Android AVD. **Four native functional tests
passed through wgpu/GLES, and all 72 benchmark phase rows matched exact RGBA and
depth controls.** This qualifies the bounded synchronous offscreen profile tested
here, not a NativeActivity/window integration, Android lifecycle, ARM64 or a
physical phone.

## Environment and admission

- Emulator 37.2.12.0 (build 16428233), Android 17/API 37 x86_64, AVD
  Medium_Phone_API_37.0, temporary `-read-only` run on port 5580, KVM, four vCPUs
  and 4 GiB RAM. The user's saved AVD configuration was not edited.
- NDK 30.0.16248370, minimum API 26, Release, `c++_shared`, legacy Coin GL renderer
  disabled, RUST_BRIDGE enabled, fresh build from this branch in
  `/tmp/coin-retained-android-wgpu`. Rust dependency pins were unchanged.
- Working GPU mode: emulator `-gpu host`, Mesa EGL and AMD Vulkan ICD on the host.
  Android SurfaceFlinger reports GLES 3.1; wgpu reports the Gl backend with the
  Android Emulator OpenGL ES Translator over AMD Radeon Graphics (radeonsi,
  renoir). Requested renderer 2 and observed renderer 2 are checked explicitly.
- The first `-gpu swiftshader` attempt found no compatible runtime adapter for
  either API and stopped before rendering. It is recorded as unsuccessful,
  not a functional pass.
- Vulkan is **not qualified**. The host-mode log says `Adapter is not Vulkan
  compliant, hiding adapter: Goldfish GFXStream (AMD Radeon Graphics (RADV
  RENOIR))`. The explicit Vulkan probe observed Gl and rejected the mismatch;
  no Vulkan rendering was counted. No noncompliant-adapter override was enabled.
- BGFX is **not tested on Android**: this base's CMake explicitly rejects the
  Android platform (`The BGFX evaluation supports Linux and Windows.`). No BGFX
  port or unrelated platform admission change was introduced.

`COIN_RETAINED_TEST_RENDERER=opengl|vulkan` was added only to the test/benchmark
harness. When specified it probes and verifies the actual API before executing;
unknown API names and unavailable/mismatching adapters fail. Desktop defaults
are unchanged. The test's existing image/depth tolerances were not relaxed.

## Functional results

| Native Android execution | Result |
| --- | --- |
| CoinRenderRetainedConnectorTest / GLES | PASS |
| CoinRenderRetainedIncrementalTest / GLES | PASS |
| Connector / GLES / composition borrow disabled | PASS |
| Incremental / GLES / composition borrow disabled | PASS |

Coverage includes the analytic coplanar color/depth control, supplied plan order,
malformed/unsupported input rejection, repeated transform/material/geometry edits,
copy-on-write isolation and bounded material slots, zero geometry transport
rebuilds for same-layout edits, inherited state, ambiguous sharing, topology
fallback, invalid patches preserving publication and recovery. Incremental frames
are compared to forced native rebuilds with exact RGBA and float depth equality.
The desktop Coin GL reference test is not an Android qualification: the legacy
GL renderer was disabled for this build. Four retained RECORDING tests also
passed on the host after the test-harness change.

Android logcat contains wgpu-hal GLES `Fake map` diagnostics. The native status,
analytic output and exact equivalence assertions passed; this report does not
claim a warning/error-free log. The raw wgpu log is retained compressed.

## Measurements

Same workload shape as the desktop follow-up: synchronous 128×128 RGBA/depth,
256 or 2,048 occurrences of one shared FaceSet triangle. Seven warmups and 31
timed frames per phase; three process trials with rotated current/retained/rebuild
order, serialized on one AVD. Editing, synchronous notifications and render with
target publication/readback are timed; output-vector copies and the independent
control render are outside timing. The control renderer/sensor is absent during
measured mutations. No builds ran alongside timed jobs.

`current` is CoinRenderAction and `retained` is the connector in the same Android
binary; `rebuild` invalidates the connector before every frame. Entries below
are medians of the three per-trial medians, milliseconds. Virtual CPU scheduling,
GLES translation and readback dominate some cases. These are emulator timings,
not physical Android/ARM performance estimates or significance-tested claims.

| Occurrences | Change | Current ms | Retained ms | Forced rebuild ms | Current/retained |
| ---: | --- | ---: | ---: | ---: | ---: |
| 256 | static | 7.432 | 5.024 | 7.102 | 1.48× |
| 256 | translation_one | 8.429 | 6.620 | 7.683 | 1.27× |
| 256 | diffuse_one | 8.708 | 9.674 | 8.491 | 0.90× |
| 256 | geometry_shared | 10.908 | 9.370 | 10.683 | 1.16× |
| 2048 | static | 5.384 | 4.373 | 19.036 | 1.23× |
| 2048 | translation_one | 27.912 | 7.464 | 19.900 | 3.74× |
| 2048 | diffuse_one | 26.206 | 8.118 | 20.320 | 3.23× |
| 2048 | geometry_shared | 29.321 | 12.355 | 25.240 | 2.37× |

The 256-occurrence diffuse case regressed about 11% (8.708 → 9.674 ms). The
2,048-occurrence edit cases improved, but smaller workloads do not uniformly
benefit. Keep the raw p90s/trial spread when assessing these numbers.

All retained measured phases recorded zero captures, zero composition
classifications and zero transport rebuilds. Only geometry phases built the
native plan (31 times per measured phase, once per edit). At 2,048 occurrences,
geometry patched 63,488 vertex positions across 31 frames and copied no indices.
The material table held one slot initially, two after a local diffuse edit.
wgpu cache-upload bytes across the 31 measured frames were:

| Change, 2,048 occurrences | Current bytes | Retained bytes |
| --- | ---: | ---: |
| static | 0 | 0 |
| translation_one | 6094848 | 6094848 |
| diffuse_one | 4960 | 4960 |
| geometry_shared | 6115432 | 6115432 |

These are the resource-cache counters, not total guest/host bus traffic.

## Reproduction and evidence

Build with NDK `android.toolchain.cmake`, `ANDROID_ABI=x86_64`,
`ANDROID_PLATFORM=android-26`, `ANDROID_STL=c++_shared`,
`CMAKE_BUILD_TYPE=Release`, `COIN_BUILD_RENDER=ON`,
`COIN_RENDER_BACKEND=RUST_BRIDGE`, `COIN_BUILD_LEGACY_GL_RENDERER=OFF`,
`COIN_BUILD_TESTS=ON`, `COIN_BUILD_SINGLE_LIB=ON`, `USE_EXTERNAL_EXPAT=OFF`.
Build the ConnectorTest, IncrementalTest and Benchmark targets named above.

Start a read-only AVD with `-no-window -no-audio -no-snapshot -no-boot-anim
-memory 4096 -cores 4 -gpu host -read-only -port 5580`, with host environment
`__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`,
`__GLX_VENDOR_LIBRARY_NAME=mesa`, and
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json` for this machine.
Wait for `sys.boot_completed=1`. Push libCoin.so, libCoinRender.so, the NDK
libc++_shared.so and the three executables into an isolated AVD directory.
For this run:

```sh
adb -s emulator-5580 shell env LD_LIBRARY_PATH=/data/local/tmp/coin-retained-20261027 COIN_RETAINED_TEST_RENDERER=opengl /data/local/tmp/coin-retained-20261027/CoinRenderRetainedIncrementalTest
adb -s emulator-5580 shell env LD_LIBRARY_PATH=/data/local/tmp/coin-retained-20261027 COIN_RETAINED_TEST_RENDERER=opengl COIN_RENDER_DISABLE_COMPOSITION_BORROW=1 /data/local/tmp/coin-retained-20261027/CoinRenderRetainedConnectorTest
adb -s emulator-5580 shell env LD_LIBRARY_PATH=/data/local/tmp/coin-retained-20261027 COIN_RETAINED_TEST_RENDERER=opengl /data/local/tmp/coin-retained-20261027/CoinRenderRetainedBenchmark retained 2048 31
```

Repeat benchmark modes current/retained/rebuild at 256 and 2048 occurrences in
rotated order over three trials. Do not carry COIN_WGPU_LOG=debug or phase tracing
into timed runs. The temporary AVD was stopped after collecting the evidence;
the saved AVD and other devices were left untouched.

Evidence: [manifest and binary/source hashes](validation/coin-render-retained-android-20261027/manifest.json),
[native commands and exits](validation/coin-render-retained-android-20261027/commands.json),
[benchmark raw JSONL](validation/coin-render-retained-android-20261027/benchmark.jsonl),
[benchmark commands](validation/coin-render-retained-android-20261027/benchmark-commands.json),
[initial SwiftShader attempt](validation/coin-render-retained-android-20261027/swiftshader-attempt.json),
[compressed wgpu log](validation/coin-render-retained-android-20261027/wgpu-logcat.log.gz).


## Visible million-building viewer

A separate NativeActivity demonstration was opened after the offscreen campaign,
using this branch's current Coin/CoinRender libraries and a freshly built host.
It uses **CoinRenderAction's window path**, not CoinRenderRetainedRenderer. The
existing deterministic city asset contains exactly 1,000,000 buildings plus a
floor (12,000,012 triangles); the offscreen content checksum was
`067c1efbc25642eb`. The visible AVD was given 8 GiB for this separate run.

The initial host reproduced an input-dispatch ANR when touch arrived during
large parsing/capture work. The viewer now acknowledges input/lifecycle on its
Android looper and queues copied motion data/acquired window references to one
render worker. All Coin scene and GPU work stays on that worker; there is no
parallel traversal/capture/planner. Consecutive MOVE events can coalesce while
DOWN/UP and lifecycle boundaries remain ordered. GPU teardown precedes releasing
the worker's acquired ANativeWindow reference.

The corrected process rendered the million, passed the existing content control,
processed three vertical zoom gestures and continued advancing frames/serials.
No ANR event was found for that corrected PID. A real emulator screenshot was
inspected and the live window was left open. The idle overview reported around
48 CPU-wall frames/s, with slower rates on the close view; this is not a GPU
timestamp benchmark or a retained-renderer million-scene qualification.
Full pause/resume/rotation/loss and cancellation-during-load qualification of the
new worker remains outside this visual check.

[Viewer receipt and binary hashes](validation/coin-render-million-android-20261027/manifest.json) ·
[Application log](validation/coin-render-million-android-20261027/application.log) ·
[Actual close-up screenshot](validation/coin-render-million-android-20261027/million-closeup.png).
