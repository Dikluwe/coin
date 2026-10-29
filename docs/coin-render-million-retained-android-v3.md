> Follow-up: [redundant schedule removal and BGFX resolved-depth admission fix](coin-render-million-retained-android-v4.md). The prior BGFX discrepancy below is resolved in v4.

# Android million-city: direct compact capture (v3)

The facade now emits compact native commands during capture, avoiding the full
command vector and its subsequent compacting pass. Native command metadata and
dependency storage are smaller. Capture and planning remain exclusively native.
The viewer still defaults to CoinRenderAction; retained remains opt-in.

## Implementation

- Enable compact storage before native traversal and preserve the preference
  across frame resets. Ordinary producers append directly to the interned table.
  Distinct-template overflow falls back to the ordinary owned representation.
- Use a fixed buffer to serialize common members before constructing the key,
  rather than invoking string append for every scalar on each command.
- Store six exact matrix coefficients (diagonal and translation) with each
  occurrence and the remaining ten in the interned key/template. Records are
  28 bytes on this ABI, down from 68. This copies captured coefficients without
  deriving them; shear, rotation and projective terms are preserved exactly.
- Share one flat node index between ownership and dependency lookup, avoiding
  per-node unordered-map allocations and the second ownership table. Nodes keep
  their original frame-owned refs. Transactional replay repairs probe clusters
  when dropping speculative nodes.
- Merge consecutive branch dependencies into inclusive command intervals,
  retaining full node/child-index path filtering for instanced branches. Use
  checked uint32 offsets/links, bit-packed authored visibility and trim excess
  capacity once per capture generation. Overflow explicitly rejects the frame.

The existing fork updater, native planner, supplied schedule, immutable frame
borrow, executor admission and target/device cache checks remain authoritative.
There is no additional capture, scene traversal/classifier or planning pass.
Supported incremental edits still patch native commands and executor transport.

Both measured v3 processes ended with:

```
captures=1 plans=1 transport_builds=1 retained_frames=132
commands=1000001 resources=2 vertices=216 cameras=1 materials=9
command_storage_bytes=28010524 path_storage_bytes=101290964
dependency_ranges=1000003 compaction_passes=0
```

Command vector/template capacity is 26.71 MiB, versus 64.86 MiB in v2. Path/index
vector capacity is 96.60 MiB, including the flat index, ownership list, saved paths,
intervals and visibility. These gauges are specific storage arrays, not total
process memory. Lazy SoPath objects and the source scene are separate.
`compaction_passes=0` counts complete dense-to-compact conversions: capacity
trimming may still copy arrays and allocate. Legacy reference/contiguous APIs
can still materialize a full array; the measured viewer does not use those APIs.

## Stabilized Android comparison

Same read-only Android API 37 x86_64 AVD, 8 GiB RAM, four vCPUs, host GPU mode,
wgpu/GLES on the AMD emulator translator. Same APK contains current and v3 retained
paths. The v2 APK is the exact previous comparison artifact. No Rust pin, shader,
scene asset, lighting, material palette, camera, resolution or sample-count change.

Scene: 1,000,000 buildings plus ground, 12,000,012 triangles, original PHONG and
two directional lights, 1080×2400, one sample. Common explicit LESS depth state.
Every process reported:

```
size=1080x2400 camera=3950.89062,2773.27783,7908.99463
focal=9260.20898 near=7275.23242 far=11245.1855
```

An initial six-process campaign showed 115,724 KiB swap in its first v2 reference
and a slower first render. It is archived under `initial-campaign/`, including raw
memory samples, rather than mixed into the summary. The six-process protocol was
then repeated with the emulator stabilized and negligible swap, with no builds
or GPU tests concurrent with measurement. Both campaigns use fresh processes in
v2/v3/current/current/v3/v2 order. The summary below uses only the second campaign.

Protocol remains: first selected render on the window; full-size offscreen
color/depth validation with the identical projection; return to the window; ten
warmups; 120 render/present calls; five dumpsys samples one second apart after
timing. Validation readback and memory sampling are outside the timed interval.
The retained instance reuses its capture, plan and CPU transport across targets.
First render excludes asset parsing, camera framing and target creation, and
includes all work inside the selected render call, including capacity trimming.

Summary is the median of the two per-process results; each memory value is itself
the median of five samples. GiB = KiB/1,048,576.

| Metric | Current capture | Retained v2 | Direct retained v3 |
|---|---:|---:|---:|
| First render call | 10.993 s | 6.297 s | 4.585 s |
| Static call median | 20.998 ms | 20.703 ms | 21.023 ms |
| Static call p90 | 22.594 ms | 22.392 ms | 22.688 ms |
| Resident memory, RSS | 3.805 GiB | 4.026 GiB | 3.864 GiB |
| Native heap allocated | 3.667 GiB | 3.899 GiB | 3.717 GiB |
| Reported total PSS | 3.695 GiB | 3.916 GiB | 3.754 GiB |
| Swap | 74.0 KiB | 74.0 KiB | 74.0 KiB |

Versus v2, first-render call time fell **27.2%** and allocated native
heap fell **4.7%**. This removes **78.5% of the v2 excess native
allocation** measured against current capture in this same campaign. Remaining
native allocation is **1.4% above current**, and RSS is **1.6% above**.
The excess is reduced, not eliminated. The adapter's detailed schedule alone is
about 53.4 MiB; this is a known resident allocation, not a full causal attribution
of the remaining process difference.

Static call times remain close to the current path. Two processes per path do
not establish a reliable small speed advantage or distinguish small regressions.
These are CPU elapsed render/present calls, not GPU timestamps, physical-phone
FPS or display-completion timestamps. Sampled load RSS peaks remain in raw
results; two-second polling is not a true peak-allocation measurement.

| Order | Mode (`previous` = v2) | First (ms) | Median (ms) | p90 (ms) | RSS (KiB) | Native allocated (KiB) |
|---|---|---:|---:|---:|---:|---:|
| 1 | previous | 6349.689 | 20.646821 | 22.405222 | 4221508 | 4088668 |
| 2 | retained | 4697.524 | 21.132339 | 23.100519 | 4051432 | 3897779 |
| 3 | current | 10785.972 | 21.099652 | 22.852443 | 3991116 | 3845618 |
| 4 | current | 11200.172 | 20.896931 | 22.336152 | 3987512 | 3845622 |
| 5 | retained | 4472.971 | 20.913798 | 22.275177 | 4051684 | 3897782 |
| 6 | previous | 6244.513 | 20.760034 | 22.379744 | 4221464 | 4088674 |

## Equivalence and validation

All six final RGBA buffers are byte-identical, and all six depth buffers are
byte-identical. Maximum error is zero, with no nonfinite depth. The initial
campaign also has zero color/depth differences. Actual display captures match
exactly after excluding the top and bottom 100 rows containing system UI.
The final v3 display was visually inspected and shows the same distant city view.

- RGBA SHA-256: `c603f4eed00512ae824c331dfbdd9f489a9dd7ace9eeca01f65a3ab692b99a20`
- Depth SHA-256: `791de884fb0c43645a4db818d907c757d07a49170934a030e905f33f13555bd3`

Targeted tests: eight recording, five desktop wgpu, eight desktop BGFX, and four
Android GLES controls passed. The city test verifies exact matrix round-tripping
including shear/projective coefficients, independent copied lists, legacy array
access, rollback, diverse fallback, and target rebinding. A common-ancestor edit
patches 400 commands, then its restoration patches 400 again, while captures=1,
plans=1, transport_builds=1 and compaction_passes=0. Single-occurrence translation
and material copy-on-write checks remain. Geometry/camera probes are independent
of that common-translation probe and use a freshly captured current reference.

A separate expanded BGFX probe deliberately keeps a decimal common translation
through later geometry and camera edits. It has eight depth samples differing
from current capture, with identical color. A dense native capture reproduces
the compact result exactly: zero matrix differences and zero different color or
depth values between dense and compact native execution. Thus this discrepancy
also exists without compact storage. It remains a native/current BGFX precision
limitation outside the fixed million-scene comparison; tolerances were not raised
to conceal it. The failing exploratory logs and dense diagnostic source/output
are preserved under `bgfx-diagnostic/`. The independent interval test restores its
transform before running the geometry/camera probes. This task does not claim
that the combined BGFX case is qualified.

This qualifies the measured emulator profile only. ARM64, physical-device memory
and timing, Android BGFX and exhaustive lifecycle/cancellation are not qualified.
The experimental native/list layout requires rebuilding Coin and consumers.

## Receipts and reproduction

[Final results and raw samples](validation/coin-render-million-retained-android-v3-20261027/results.json),
[exact comparisons and binary hashes](validation/coin-render-million-retained-android-v3-20261027/analysis.json),
and [source/APK manifest](validation/coin-render-million-retained-android-v3-20261027/manifest.json)
are stored with per-frame times, logs, PNGs, compressed raw readbacks for one
example per path, separate profiling traces, test receipts, the initial campaign
and the BGFX investigation. No APK or signing key is stored in the repo.

Measured v3 APK SHA-256: `2c780da1dfdbd4d4d0758f57b6d1e37bec5706185775db8b17b23582ad0f98ac`.
Previous v2 APK: `c9aa9a135ee6e6976534094f4ba33709672467272ed065d6e43b910198937ae9`.
The APKs remain under `/tmp/coin-retained-million-v3-final/`.
The receipt's `measured-runner.py` records every ADB command and process order;
adjust local SDK/APK/output paths for another machine. Intent options and Android
build details remain in [the original comparison](coin-render-million-retained-android.md#receipts-and-reproduction).
The profiling run is separate from the timed six processes and confirms the
24-vertex/36-index mesh, immutable preparation hits and zero static uploads.
