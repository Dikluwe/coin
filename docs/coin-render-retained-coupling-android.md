# Broader retained coupling and Android million-city comparison

The existing branch now couples additional native shapes, lights, fog, clipping,
materials, stored 2D textures, strokes and execution policies to the same wgpu
and BGFX executors. There is one native capture and one native order planner.
Qualified projection changes update native camera state and owned transport
without capturing, planning or copying geometry again. This is a broader opt-in
profile; the complete current renderer is not yet coupled.

See the [coverage and remaining work](coin-render-retained-integration.md#current-coverage)
for exact boundaries: shadows/bump, RTT/cube/volume/multitexture, layers/barriers,
selection/picking, packed colors, surface raster styles and manager/async/multipass
orchestration still need native data and operation contracts. Unsupported semantics
are refused during the sole traversal or transport admission.

## Controlled Android comparison

API 37 x86_64 read-only AVD, 8 GiB, four virtual CPUs, host GPU, explicit
wgpu/GLES renderer 2 (observed 2) on the AMD emulator translator. Same APK for
current and retained, 1,000,000 buildings plus ground, 12,000,012 triangles,
original PHONG/two directional lights, explicit LESS depth, one sample,
1080 x 2400 and the same camera in all six processes:

```
camera=3950.89062,2773.27783,7908.99463
focal=9260.20898 near=7275.23242 far=11245.1855
```

Initial four-process order: current/retained/retained/current. One further process
of each path was added after observing swap in the first current process; all six
are included, with no selective exclusion. The extra current process parsed its
scene while an unrelated control build finished; that build completed at
23:02:12.774 emulator-local time, before the timed render began at 23:02:19.600.
No builds or other GPU tests overlapped the timed render/present intervals.

First render is the selected render call on the actual window, excluding scene
parsing, framing and target creation. Then full-resolution RGBA/depth readback on
an identical-projection offscreen target, rebind to the window, ten warmups, 120
render/present calls, and five memory samples one second apart. Analysis and
readback are outside timed intervals. Summary is the median of three process
values; each process memory value is first the median of its five samples.

| Metric | Current capture | Retained capture |
| --- | ---: | ---: |
| First render | 9.715 s | 4.354 s |
| Static median | 19.829 ms | 19.688 ms |
| Static p90 | 20.777 ms | 20.918 ms |
| Native allocated heap | 3.667 GiB | 3.654 GiB |
| Resident memory (RSS) | 3.810 GiB | 3.805 GiB |
| Reported total PSS | 3.695 GiB | 3.687 GiB |
| Swap | 47 KiB | 0 KiB |

The retained first call is 55.2% shorter. Static time is close;
three processes per path do not establish a meaningful sub-percent advantage.
Native allocated heap is 13.90 MiB lower (0.37%).
The overall process remains large because scene graph and executor allocations
still dominate. The first current process had 219,376 KiB swap and lower RSS;
RSS alone is not evidence of an integration allocation advantage. The aggregate
RSS difference is 5.71 MiB, much smaller than that paging variation.

| Trial | Path | First (ms) | Static median (ms) | RSS (KiB) | Native allocated (KiB) | Swap (KiB) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | current | 9441.559 | 19.340335 | 3777068 | 3845631 | 219376 |
| 2 | retained | 4281.871 | 19.591046 | 3989380 | 3831373 | 0 |
| 3 | retained | 4483.794 | 19.743007 | 3989448 | 3831389 | 0 |
| 4 | current | 9714.631 | 19.828883 | 3996872 | 3845620 | 0 |
| 5 | current | 9846.478 | 19.901014 | 3995224 | 3845624 | 47 |
| 6 | retained | 4354.441 | 19.687738 | 3986308 | 3831396 | 47 |

Every retained process reports:

```
captures=1 plans=1 transport_builds=1 retained_frames=132
commands=1000001 resources=2 vertices=216 cameras=1 materials=9
command_storage_bytes=28014748 path_storage_bytes=89290928
compaction_passes=0 schedule_bytes=0
```

Expanded captured state adds 4,224 bytes of common native templates and eight
bytes of world-dependency path storage relative to v4, without adding per-draw
schedule records. The native allocated heap is effectively unchanged from v4.
This campaign has no fresh v4 process, so it does not establish a timing change
relative to that historical APK. Sampled RSS load peaks are saved but two-second
polling is not a true peak-memory measurement. These are elapsed calls in an
emulator, not GPU timestamps, physical-device FPS or ARM64 qualification.

## Equivalence and validation

All six RGBA buffers and all six depth buffers are byte-identical, with zero
nonfinite depth values. Window screenshots match outside the top/bottom 100
rows of system UI. The full-resolution retained capture was visually inspected.

- RGBA SHA-256: `c603f4eed00512ae824c331dfbdd9f489a9dd7ace9eeca01f65a3ab692b99a20`
- Depth SHA-256: `791de884fb0c43645a4db818d907c757d07a49170934a030e905f33f13555bd3`

The integration campaign passes eight recording tests, 21 wgpu tests, 23 BGFX
tests and six Android GLES runs. The main feature test covers 64 visible scenes,
static reuse and translations, plus switch visibility/inherited state, hidden
material edits, texture-quality transitions and qualified camera projection
updates in both ordinary and compact command storage. GPU policy profiles cover
MSAA4, sampling, canonical coverage, sRGB, weighted OIT, peeling and native-order
transparency modes. The city test keeps exact BGFX depth comparison.

An additional campaign of existing current-path tests passed 14 controls after
explicitly enabling the optional camera control. One control remains failing:
`CoinRenderTextureSamplingGpuTest`, projective mip footprint at quality 0.5 and
offset zero, against legacy CoinGL (MAE 8.08, maximum 78); CPU and wgpu agree
exactly in that case. Its initial GLX context failure was resolved using the same
pixmap policy as the quality controls. This broader legacy-oracle discrepancy
has not been resolved or proven to predate the branch; it is not hidden by
increasing tolerances. The dedicated retained-vs-current tests pass.

## Receipts

[Coverage/contracts](coin-render-retained-integration.md),
[measurement results](validation/coin-render-retained-coupling-20261027/million/results.json),
[exact comparisons](validation/coin-render-retained-coupling-20261027/million/analysis.json)
and [source/APK manifest](validation/coin-render-retained-coupling-20261027/manifest.json)
are stored with runner scripts, process logs, memory samples, 120 timings per
process, screenshots, representative compressed raw buffers, build logs and
regression receipts. No APK or signing key is checked in.
