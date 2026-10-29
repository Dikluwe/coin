> Follow-up: [memory and static-frame optimizations with a fresh three-path comparison](coin-render-million-retained-android-v2.md). The results below describe the previous connector.

# Retained fork capture in the Android million-city viewer

The fork capture is connected to the Android window viewer through the existing
wgpu executor. It produces exactly the same color and depth as CoinRenderAction
for the original million-building scene and camera. This integration reduces
first-render work, but **does not reduce steady-state memory or static frame time**
in this experiment. The current capture remains the default.

## Implementation

- `coinrender_retained=true` selects `CoinRenderRetainedRenderer` in the existing
  NativeActivity worker. One thread still owns scene state, capture and GPU work.
- Logical directional lights, PHONG normals/materials and window targets are now
  admitted by the bounded connector. The two original lights and material palette
  are preserved. Other unsupported semantics still fail explicitly.
- Shared SoCube nodes publish native geometry resources. The adapter shares
  transport spans per resource/material and interns the camera. Material changes
  use copy-on-write for shared geometry spans; geometry updates patch owned spans.
- The fork action and planner remain authoritative. Window → same-size offscreen
  validation → window reuses the same native capture, plan and CPU transport.
  Target/device ownership and GPU cache checks remain in the executors.
- An unchanged frame uses a cached schedule-identity predicate. It does not walk
  one million schedule entries to rediscover their identity each frame.

At the end of both retained trials the counters were identical:

```
captures=1 plans=1 transport_builds=1 retained_frames=132
commands=1000001 resources=2 vertices=324 cameras=1 materials=9
```

The two native resources are the shared building cube and the ground cube. The
324 transport vertices are nine material/resource variants × 36 triangle vertices.
This does not mean the scene graph, command list or instance data have disappeared:
there are still 1,000,001 commands and a million distinct transforms.

## Controlled comparison

2026-10-27, one APK, one package, four fresh processes in retained/current/current/
retained order. Each process loads the identical `city-1000000.iv`: 1,000,000
buildings plus ground, 12,000,012 triangles. Resolution 1080×2400, one sample,
original PHONG and two directional lights. Both paths have an explicit default
SoDepthBuffer (LESS), since their implicit depth defaults differ. No geometry,
material palette, lighting or camera is changed to simplify retained capture.

Identical camera/viewport log in all four trials:

```
size=1080x2400
camera=3950.89062,2773.27783,7908.99463
focal=9260.20898 near=7275.23242 far=11245.1855
```

Platform: Android API 37 x86_64, Medium_Phone_API_37.0 read-only AVD, 8 GiB RAM,
four vCPUs, emulator 37.2.12.0, host GPU mode. The executor reports wgpu GLES,
Android Emulator OpenGL ES Translator on AMD Radeon Graphics/radeonsi.
Vulkan is not substituted for GLES. BGFX does not admit Android in this build.

For each process, time the first selected render on the actual window. Then
read RGBA/depth from a separate target with the same dimensions/projection,
return to the window, exclude ten warmups and measure 120 render/present calls.
Readback and dumpsys are outside that timed interval. Collect five memory samples
one second apart after the timed interval while the app continues rendering.
Both current-path setters are guarded against equal values because their public
implementation invalidates capture even for identical arguments. An exploratory
run before that harness correction was discarded and is not used below.

First-render timing excludes asset parsing, camera framing and target creation;
it includes capture/planning, executor preparation and rendering/presentation.
Steady timing is elapsed call time, not GPU timestamp duration or a physical
phone's FPS. The summary takes the median of the two per-process results; each
per-process memory value is itself the median of five samples. GiB = KiB/1,048,576.

| Metric | Current capture | Fork retained capture | Change |
|---|---:|---:|---:|
| First render | 10.438 s | 6.086 s | −41.7% |
| Static render/present call, median | 20.083 ms | 27.567 ms | +37.3% time |
| Static call, p90 | 21.050 ms | 28.559 ms | +35.7% time |
| Resident memory, RSS | 3.812 GiB | 4.577 GiB | +20.1% |
| Reported total PSS | 3.696 GiB | 4.461 GiB | +20.7% |
| Native heap allocated | 3.667 GiB | 4.515 GiB | +23.1% |
| Swap | 0 | 0 | Same |

Individual runs:

| Order | Capture | First render (ms) | Median (ms) | p90 (ms) | RSS (KiB) | Native heap allocated (KiB) |
|---|---|---:|---:|---:|---:|---:|
| 1 | Retained | 6061.431 | 27.676567 | 28.542520 | 4800032 | 4734255 |
| 2 | Current | 10422.642 | 19.999640 | 21.078778 | 3999348 | 3845622 |
| 3 | Current | 10454.181 | 20.166108 | 21.020448 | 3995852 | 3845630 |
| 4 | Retained | 6110.918 | 27.458337 | 28.575622 | 4799600 | 4734253 |

Sampled load RSS peaks were lower for retained (4,960,912 / 4,897,072 KiB) than
current (5,160,552 / 5,183,200 KiB). These are approximately two-second samples,
include the qualification target transitions, and are not reliable true peaks.
They do not reverse the steady resident-memory regression.

## Image and depth equivalence

All four full-resolution RGBA buffers are byte-identical, and all four depth
buffers are byte-identical. There are zero different color channels, zero
different depth values, no nonfinite depth, and maximum error zero.

- RGBA SHA-256: `c603f4eed00512ae824c331dfbdd9f489a9dd7ace9eeca01f65a3ab692b99a20`
- Depth SHA-256: `791de884fb0c43645a4db818d907c757d07a49170934a030e905f33f13555bd3`

The actual Android display was also captured independently of readback. The
current and retained display pixels match exactly after excluding the top and
bottom 100 rows containing system UI. The retained screenshot was visually
inspected and shows the city. These images preserve the requested common camera;
the distant overview makes individual buildings small.

[Retained Android display](validation/coin-render-million-retained-android-20261027/4-retained/display.png)

## Validation and limits

- Recording executor: seven tests passed (city equivalence/retargeting,
  incremental updates, connector, action, backend boundary, transform, composition).
- Desktop wgpu: five retained tests passed, including independent GL reference
  and disabled-composition-borrow control.
- Desktop BGFX: eight tests passed, including the retained city, incremental,
  connector, no-borrow, GL reference, backend boundary, core and instancing tests.
- Android GLES: city equivalence/retargeting, incremental, connector and no-borrow
  controls passed. The small PHONG city has zero observed color/depth error
  before and after shared material, cube geometry and camera updates.

This qualifies the measured Android emulator configuration. It does not qualify
ARM64, physical-device memory/performance, BGFX on Android, or all lifecycle and
cancellation cases. The retained API remains experimental and requires rebuilding
Coin and its consumers because native IR hooks add virtual methods.

The adapter keeps native IR/planning state and executor transport resident. This
experiment measures their combined cost; it does not attribute the extra memory
or static-frame time to a particular allocation or CPU function. Native geometry
sharing and unique capture/planning are working, but they have not produced a
steady-state win at one million instances. Keep both paths available while
profiling command/state/instance residency and the static executor path.

## Receipts and reproduction

[Raw results, analysis, logs, memory samples and hashes](validation/coin-render-million-retained-android-20261027/results.json)
are saved beside per-run timings, actual-display PNGs, full-resolution readback
PNGs and compressed raw RGBA/depth for one pair. `analysis.json` records exact
comparison and all raw-buffer hashes. `manifest.json` identifies the APK, scene,
branch/base and changed implementation files. Test logs are saved in the same
folder. Large APK/debug binaries and the signing key are excluded from the repo.
The measured APK remains at `/tmp/coin-retained-million-ab/coin-render-million-ab.apk`.

The exact measured runner is `measured-runner.py` beside the receipts. It records
all ADB commands in `commands.json`; adjust its local SDK/APK/output constants to
repeat on another machine. The analyzer uses NumPy/Pillow. Build the Android
window example with the existing Android configuration, package the million-city
asset and select either path through the same APK:

```sh
adb -s emulator-5580 shell am force-stop org.coin3d.coinrender.retainedmillion
adb -s emulator-5580 shell am start -f 0x10008000 \
  -n org.coin3d.coinrender.retainedmillion/android.app.NativeActivity \
  --ei coinrender_renderer 2 --ei coinrender_samples 1 \
  --ez coinrender_city true --ez coinrender_million_city true \
  --ez coinrender_continuous true --ez coinrender_compare true \
  --ez coinrender_retained true
```

Use `coinrender_retained false` for the current path. Both runs write the same
camera and viewport, measured timing and selected backend to `CoinRenderP23` logcat;
readback/timing files go into the app's internal `files/` directory.
