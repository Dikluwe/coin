> Follow-up: the retained capture is now connected to the million-city Android
> window viewer. See [the controlled same-APK comparison](coin-render-million-retained-android.md).
> The measurements and scope below describe the earlier experiment.

# Android million-building memory comparison — 2026-10-27

The million-building visualizer still uses **CoinRenderAction**, not the retained
FreeCAD capture connector. This compares the previously qualified 2026-10-23 APK
with the current branch's APK (including the viewer event/render worker fix).
They are different complete builds, not a FreeCAD-only ablation.

Both ran sequentially, one app at a time, in the same fresh read-only Android
x86_64/API 37 AVD: 8 GiB configured RAM, four vCPUs, AMD host GPU, wgpu/GLES,
1080×2400, 1× sampling, continuous rendering, one million buildings plus ground.
Both reported 12,000,012 triangles, advancing frames and the same 64×64 content
checksum `067c1efbc25642eb`. No gestures were sent during these measurements.

Five `dumpsys meminfo` samples per live process were taken after the first
validated output and at least 20 frames. The old process had a longer warmup
because its renders were slower and the initial observer waited longer; paging
was not forced into an identical state. Values below are sample medians, GiB.
The old APK was 0.42 CPU-wall frames/s at the end of its warmup; this is not a
performance benchmark. RSS was also polled during load, without a complete peak
allocation trace. Only one launch per build was sampled.

| Metric | Previous APK | Current branch | Change |
| --- | ---: | ---: | ---: |
| Native heap allocated | 4.028 | 3.928 | -2.5% |
| Resident memory (RSS) | 3.442 | 4.076 | +18.4% |
| Swap / SwapPSS | 0.798 | 0.000 | -100.0% |
| TOTAL PSS as reported by Android | 4.122 | 3.958 | -4.0% |

The native allocation decrease is small (about 2.5%, roughly 102 MiB). RSS is
higher, with the former APK having about 0.80 GiB paged out and the new process
having none in these samples. This does not establish a meaningful reduction in
physical RAM use, and it cannot be attributed to the FreeCAD connector.

On this Android image the old `TOTAL PSS` includes swapped-out PSS: summing its
resident category PSS plus `TOTAL SWAP PSS` reproduces that total. The new dump
has zero swap and labels the column `Swap`. Thus the reported total must not be
read as an exclusively resident-RAM figure or used alone to claim physical RAM
savings. The raw dumps preserve all categories and headers. PSS/SwapPSS and RSS
also account for sharing differently; they are not interchangeable quantities.

8 GiB is the configured memory of the whole emulator, not the measured heap or
RSS of the app. Host QEMU, graphics translator and GPU allocations can sit outside
these application figures. No system paging policy, zram settings or application
allocator behavior was changed to manufacture a result.

The [receipt](validation/coin-render-million-memory-android-20261027/results.json)
contains every sample, binary hashes, sampled RSS maxima and exact limits. Raw
`dumpsys meminfo` files and compressed ADB command logs are in the same directory.
The two APK packages are `org.coin3d.coinrender.p23` (previous) and
`org.coin3d.coinrender.retainedmillion` (branch). Each was force-stopped after its
measurement; the temporary AVD was then stopped. Saved AVD settings were not edited.
