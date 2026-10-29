# Retained v4: memory parity and BGFX depth correction

The measured retained path no longer exceeds current capture in allocated native
heap, total reported PSS or resident memory. Its capture, updater and planner
remain the fork's sole planning authority. The original combined BGFX regression
now passes with exact depth equality; its transform is no longer reset to isolate
the later geometry/camera probes.

## Storage changes

The adapter no longer persists one 56-byte CoinRenderCompositionItem per draw.
Its owned frame already follows the native operation order. Identity order is
represented without an index array; a non-identity update may retain a uint32
permutation. Composition views derive range/depth fields from the owned draw and
state on demand, without classifying, sorting or traversing again. Incremental
range changes therefore need no duplicate schedule-field patching.

Frame/revision/policy matching and lexical authority remain mandatory. Compact
order can only be bound by the connector or Target. Legacy vector consumers can
materialize a temporary resolved sequence, while normal vector loans preserve
reference identity. Wgpu/BGFX lowering consume the compact view directly. This
removes about 53.4 MiB of persistent schedule storage for the million scene.
The ordinary current-capture classifier remains available for ordinary callers;
no revision alone grants a native resolved-order receipt.

The flat native node index now owns ordinary path-node refs itself, removing the
second ordinary ownership vector. A small separate list tracks only speculative
replay additions for rollback. Path lengths are derived from adjacent saved
start offsets (or the frame path-array end), removing another uint32 per command.
This preserves zero-length paths, append/truncate and transactional replay.
These path changes remove another ~11.44 MiB in this scene.

Both measured v4 processes reported:

```
captures=1 plans=1 transport_builds=1 retained_frames=132
commands=1000001 resources=2 vertices=216 cameras=1 materials=9
command_storage_bytes=28010524 path_storage_bytes=89290920
dependency_ranges=1000003 compaction_passes=0 schedule_bytes=0
```

The zero schedule gauge describes persistent adapter storage, not zero logical
execution items. Capacity trimming and legacy temporary vector expansion may
still allocate; there is no second planner or full post-capture command rebuild.

## BGFX diagnosis and correction

Before the fix, the current path's explicit SoDepthBuffer produced
`explicitDepthMask=15`. BGFX declined instancing solely because that mask was
nonzero, even though test/write/function/range matched the retained path's
resolved state. The current path used 400 ordinary draws while retained used one
instanced group. Matrices, positions and indices matched before lowering, but
the different position-evaluation routes produced eight depth differences after
a decimal common translation, geometry edit and camera move. Disabling the
camera-patch optimization did not remove the discrepancy.

BGFX now admits known authored-depth mask bits when all the existing opaque
instancing checks on resolved test/write/function/range pass. Camera-patch
admission follows the same rule. Unknown bits, disabled writes, unsupported
functions/ranges, alpha/transparent/special draws and other prior restrictions
remain disqualified. This corrects the inconsistent batch choice for equivalent
resolved state, preserving instancing instead of forcing the retained path into
ordinary expanded geometry.

The city regression keeps the entire combined edit sequence. BGFX compares every
depth float exactly, in addition to the original color/depth tolerance checks;
tolerances were not increased. All eight comparison stages pass. The separate
lowering test covers masks 0,1,2,4,8,15, rejects unknown bit 16 and verifies that
disabled depth writes remain unqualified. This does not claim universal bitwise
identity between forced general and instanced GPU evaluation for every scene.

## Android million-city comparison

Six fresh processes in v3/v4/current/current/v4/v3 order, with no concurrent
builds or GPU tests. V3 uses the exact previous APK; current and v4 share the new
APK. Same Android API 37 x86_64 read-only AVD, 8 GiB RAM, four vCPUs, host GPU,
wgpu/GLES on the AMD emulator translator. BGFX correctness is tested on desktop;
this build does not support BGFX on Android.

Same 1,000,000 buildings plus ground, 12,000,012 triangles, original PHONG and
two directional lights, one sample, common explicit LESS depth state,
1080×2400. Every process reported the same camera:

```
camera=3950.89062,2773.27783,7908.99463
focal=9260.20898 near=7275.23242 far=11245.1855
```

Protocol: first selected render on the actual window; full-resolution color/depth
validation on an identical-projection offscreen target; back to the window;
ten warmups; 120 timed render/present calls; five memory samples one second apart
after timing. Readback, dumpsys and comparison analysis are outside the timed
interval. The retained capture, plan and CPU transport are reused across targets.
First render excludes scene-file parsing, camera framing and target creation.
Summary is the median of two per-process values; memory is first aggregated as
the median of five samples per process. GiB = KiB/1,048,576.

| Metric | Current capture | Previous retained v3 | Retained v4 |
|---|---:|---:|---:|
| First render call | 9.496 s | 4.141 s | 4.107 s |
| Static call median | 19.647 ms | 19.708 ms | 19.653 ms |
| Static call p90 | 20.468 ms | 20.653 ms | 20.676 ms |
| Resident memory, RSS | 3.815 GiB | 3.870 GiB | 3.806 GiB |
| Native heap allocated | 3.667 GiB | 3.717 GiB | 3.654 GiB |
| Reported total PSS | 3.696 GiB | 3.750 GiB | 3.687 GiB |
| Swap (median samples) | 0 KiB | 0 KiB | 0 KiB |

V4 is 0.37% below current capture in allocated native heap and
0.24% below it in RSS. Relative to v3, native allocation fell
64.85 MiB and RSS fell 64.92 MiB.
The excess is eliminated in this controlled measurement. The small margin below
current is not evidence of a large total-process memory advantage: scene graph
and executor data still dominate. Static time is effectively at parity; two
processes per path do not establish a meaningful small speed difference. The
first-render advantage over current capture is preserved.

| Order | Mode (`previous` = v3) | First (ms) | Median (ms) | p90 (ms) | RSS (KiB) | Native allocated (KiB) |
|---|---|---:|---:|---:|---:|---:|
| 1 | previous | 4167.840 | 19.688790 | 20.573218 | 4056960 | 3897783 |
| 2 | retained | 4145.542 | 19.700196 | 20.776049 | 3990636 | 3831378 |
| 3 | current | 9719.965 | 19.493342 | 20.258295 | 4000180 | 3845632 |
| 4 | current | 9272.577 | 19.800826 | 20.677744 | 4000744 | 3845638 |
| 5 | retained | 4068.619 | 19.605999 | 20.575101 | 3991336 | 3831367 |
| 6 | previous | 4114.667 | 19.726541 | 20.732948 | 4057976 | 3897781 |

These are elapsed render/present calls, not GPU timestamps or physical-phone FPS.
Sampled load RSS peaks are saved but two-second polling is not a true peak-memory
measurement. Results apply to this emulator/GLES profile; ARM64, physical-device
performance and exhaustive lifecycle/cancellation are not qualified.

## Equivalence and checks

All six 1080×2400 RGBA buffers are byte-identical. All six depth buffers are
byte-identical, with zero maximum error and no nonfinite values. Actual display
captures also match after excluding the top/bottom 100 rows of system UI.
The v4 window screenshot was visually inspected.

- RGBA SHA-256: `c603f4eed00512ae824c331dfbdd9f489a9dd7ace9eeca01f65a3ab692b99a20`
- Depth SHA-256: `791de884fb0c43645a4db818d907c757d07a49170934a030e905f33f13555bd3`

Eight recording, five desktop wgpu, eight desktop BGFX and four Android controls
passed. Coverage includes the combined depth regression, resolved-depth masks,
legacy vector-reference identity, reversed supplied-plan output, no-borrow/raw
mutable ingress controls, single-command and shared-ancestor incremental updates,
material copy-on-write, geometry changes, target rebinding, replay rollback,
exact matrix reconstruction and native unique capture/planning counters.
The experimental native/internal layout requires rebuilding Coin and consumers.

## Receipts

[Results and samples](validation/coin-render-million-retained-android-v4-20261027/results.json),
[exact comparisons and hashes](validation/coin-render-million-retained-android-v4-20261027/analysis.json),
and [source/APK manifest](validation/coin-render-million-retained-android-v4-20261027/manifest.json)
are stored with all process logs, 120 frame times per process, display/readback
PNGs, compressed raw RGBA/depth for one example per path, before/after traces,
test logs and the depth diagnosis. Profiling is separate from timed comparison.
No APK, signing key or large build output is added to the repo.

V4 APK: `9dc873128a5fa115cf9003b751a0e4aca0b4de09b344f5c562c30371532d864d`.
V3 reference APK: `2c780da1dfdbd4d4d0758f57b6d1e37bec5706185775db8b17b23582ad0f98ac`.
The tested APK remains at `/tmp/coin-retained-million-v4/coin-render-million-ab.apk`.
The receipt runner records every ADB command and process order; adjust local
SDK/APK/output paths to repeat elsewhere. Android build/intent options remain in
[the original reproduction instructions](coin-render-million-retained-android.md#receipts-and-reproduction).
