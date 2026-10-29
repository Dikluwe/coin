> Follow-up: [direct compact capture and smaller dependency storage](coin-render-million-retained-android-v3.md). Results below describe v2.

# Android retained million-city optimization — second comparison

The retained connector now renders static frames at essentially the current
capture's measured time, with substantially less memory than its previous
version. It still uses more steady-state memory than the current capture.
The default path remains CoinRenderAction.

## Changes and diagnosis

Separate traced runs of the previous APK showed that both paths already hit
immutable preparation and Rust-owned scene receipts, with zero static geometry,
instance and material uploads. Target validation was only a few microseconds;
the substantial time was in submit/present. Their submitted indexed meshes were
different: current capture had 24 vertices/36 indices, whereas retained capture
had 36 vertices/36 indices, despite drawing exactly the same cube triangles.
This repeated unnecessary vertex work across 1,000,001 instances.

The retained SoCube recipe now emits the original face corners, normals and UVs
with four vertices per face and six indices per face. The after-change trace
confirms one canonical mesh with **24 vertices and 36 indices**, one instanced
group, and the same zero-upload static receipts as the current path. No shader,
Rust dependency pin, scene, camera, light, material palette or sample count changes.

Native SoRenderCommand is 656 bytes on this ABI, in addition to the existing
executor transport. The private facade now interns repeated command members and
stores a **68-byte occurrence entry** containing its model matrix and template
index. The million scene's command-vector capacity plus templates is **68,010,564
bytes (64.86 MiB)**. The uncompressed logical commands alone require at least
656,000,656 bytes (625.61 MiB), before capacity slack. This is exact template
sharing, not lossy compression or source-scene removal.

The native planner, updater and connector use value snapshots. Translation and
material updates replace individual compact entries, keeping unrelated commands
compact. Legacy reference/contiguous-pointer access still materializes an ordinary
owned array on demand. Small/diverse lists keep that ordinary representation;
unsuccessful compaction is not retried each static frame. Template history has a
4096-entry collection threshold. The optional compacting allocation cannot fail
an already successful target publication. The connector also reserves its
state/draw/schedule upper bound directly from the supplied operations.

All work uses the same fork capture, updater and planner. The compacting pass
encodes already captured command values; it does not traverse scene nodes,
classify composition, sort, recapture or plan again. At the end of both optimized
million trials:

```
captures=1 plans=1 transport_builds=1 retained_frames=132
commands=1000001 resources=2 vertices=216 cameras=1 materials=9
command_storage_bytes=68010564
```

The 216 transport vertices are nine material/resource variants × 24 vertices.
One million distinct transforms, the scene graph and native dependency paths
remain resident. Native indices and revisions retain their previous semantics.

## Fresh three-path comparison

Six fresh processes, in this order: previous retained, optimized retained,
current, current, optimized retained, previous retained. The previous retained
APK is exactly the receipt from the first comparison (SHA-256 starts
`c95c4db6fc05`); the current and optimized retained paths share the new APK
(SHA-256 starts `c9aa9a135ee6`). No historical timing is mixed into this table.
Tracing is disabled for these six measured processes. There were no concurrent
builds or GPU tests during this campaign.

Same read-only Android API 37 x86_64 AVD, 8 GiB, four vCPUs, host GPU,
wgpu/GLES on the AMD emulator translator. Same asset: 1,000,000 buildings plus
ground, 12,000,012 triangles, PHONG, two original directional lights, one sample,
1080×2400. Both paths retain the common explicit LESS depth state used in the
first comparison. All six logs report the identical camera:

```
camera=3950.89062,2773.27783,7908.99463
focal=9260.20898 near=7275.23242 far=11245.1855
```

Protocol: first render on the window; separate full-resolution color/depth
validation at the identical projection; return to the window; ten warmups;
120 timed render/present calls; five dumpsys memory samples one second apart.
The capture target is destroyed before static timing. Readback and memory
sampling are outside that timed interval. Each summary is the median of the two
per-process results, with memory first aggregated across the five samples.
GiB = KiB/1,048,576.

| Metric | Current capture | Previous retained | Optimized retained |
|---|---:|---:|---:|
| First render | 10.291 s | 5.709 s | 6.401 s |
| Static call median | 20.764 ms | 27.765 ms | 20.813 ms |
| Static call p90 | 22.624 ms | 29.515 ms | 22.093 ms |
| Resident memory, RSS | 3.801 GiB | 4.568 GiB | 4.020 GiB |
| Reported total PSS | 3.697 GiB | 4.463 GiB | 3.916 GiB |
| Native heap allocated | 3.667 GiB | 4.515 GiB | 3.899 GiB |
| Swap | 146 KiB | 144 KiB | 144.5 KiB |

Relative to previous retained: **25.0% less static frame time, 12.0% less RSS,
13.6% less allocated native heap**. This removes **71.4% of the previous excess
RSS** and **72.7% of the previous excess native allocation**, measured against
current capture in this same campaign.

Relative to current capture: static call time differs by only +0.24%, with
overlapping per-run ranges. Treat this as approximate parity, not evidence of a
speed advantage. RSS is still **5.8% higher** and native allocation **6.3% higher**.
First render remains **37.8% faster than current capture**, but is **12.1% slower
than previous retained**: building compact storage adds first-frame work. The
optimization trades some first-render speed for lower retained residency.

Individual measurements:

| Order | Path | First (ms) | Median (ms) | p90 (ms) | RSS (KiB) | Native allocated (KiB) |
|---|---|---:|---:|---:|---:|---:|
| 1 | Previous retained | 5678.694 | 28.103034 | 30.278333 | 4789756 | 4734250 |
| 2 | Optimized retained | 6243.111 | 20.703151 | 21.840480 | 4216136 | 4088655 |
| 3 | Current | 10622.135 | 20.361009 | 22.141486 | 3985144 | 3845615 |
| 4 | Current | 9959.193 | 21.167090 | 23.107312 | 3986576 | 3845630 |
| 5 | Optimized retained | 6559.335 | 20.923121 | 22.345991 | 4215368 | 4088656 |
| 6 | Previous retained | 5738.629 | 27.426212 | 28.751302 | 4789848 | 4734262 |

These are elapsed render/present calls, not GPU timestamp duration, display
completion timestamps or physical-phone FPS. First render excludes asset parsing,
camera framing and target creation. Two processes per path are sufficient to
expose the large regression and improvement here, but not to distinguish small
performance differences. Sampled load RSS peaks are recorded in raw results;
two-second polling is not a reliable measurement of true peak allocation.

## Equivalence and regressions

All six full-resolution RGBA buffers are byte-identical. All six depth buffers
are byte-identical, without nonfinite depth. Maximum color/depth error is zero.
Actual Android display captures also have zero different channels after excluding
the top/bottom 100 rows containing system UI; the optimized display was visually
inspected and shows the original distant city overview.

- RGBA SHA-256: `c603f4eed00512ae824c331dfbdd9f489a9dd7ace9eeca01f65a3ab692b99a20`
- Depth SHA-256: `791de884fb0c43645a4db818d907c757d07a49170934a030e905f33f13555bd3`

[Optimized Android display](validation/coin-render-million-retained-android-v2-20261027/5-retained/display.png)

Targeted verification:

- Recording executor: eight tests passed, covering city, incremental updates,
  connector/no-borrow, action, backend boundary, transform and composition.
- Desktop wgpu: five retained tests passed, including GL reference and no-borrow.
- Desktop BGFX: eight tests passed, including city, incremental, connector,
  no-borrow, GL reference, backend boundary, core and instancing.
- Android GLES: city, incremental, connector and no-borrow passed. The city test
  verifies compact storage, copied-list ownership, legacy expansion, checkpoint
  rollback, diverse-list fallback, target rebinding, and exact observed pixels
  after edits. A single-occurrence translation and material edit each patch one
  command with captures=1, plans=1, transport_builds=1. Shared material geometry
  uses copy-on-write; cube and camera changes retain their existing conservative
  invalidation policy.

The default path stays unchanged. This is an emulator qualification; ARM64,
physical-device performance, Android BGFX, exhaustive lifecycle and cancellation
remain outside the measured scope. The experimental native/list ABI requires
rebuilding Coin and consumers. The exact template key must be updated if new
SoRenderCommand state members are introduced.

## Evidence and remaining costs

[Results and samples](validation/coin-render-million-retained-android-v2-20261027/results.json),
[exact buffer comparison and hashes](validation/coin-render-million-retained-android-v2-20261027/analysis.json),
and [source/APK manifest](validation/coin-render-million-retained-android-v2-20261027/manifest.json)
are saved with all six application logs, per-frame times, display/readback PNGs,
compressed raw color/depth for one example of each path, compressed before/after
phase traces, test logs and the exact measured runner. The profiling runs are
separate from the six timed runs. Their timing is diagnostic only.

The tested APK is `/tmp/coin-retained-million-v2/coin-render-million-ab.apk`;
`previous.apk` beside it is the original comparison artifact. APKs, large build
outputs and signing keys are not stored in the repo. Reproduction uses the same
intent flags and Android build described in
[the first comparison](coin-render-million-retained-android.md#receipts-and-reproduction).
The receipt's `measured-runner.py` records the process/APK order and all ADB
commands; adjust its local paths before running elsewhere.

Residual residency includes the compact native commands, native dependency/path
indices, supplied plan, adapter schedule and executor transport. The experiment
does not fully attribute the remaining ~0.22 GiB of excess RSS to each of those
allocations. Further reduction should focus on these representations while
preserving the single native planning authority and per-command update support.
