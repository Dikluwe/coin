# Retained connector measurements — 2026-10-27

Historical first connector results. See the [geometry/material follow-up](coin-render-retained-benchmark-v2.md) for the current implementation and new measurements.

These are local synthetic results for the admitted opaque unlit triangle profile.
They compare `CoinRenderAction` (`current`), the new retained entry point
(`retained`), and that same entry point invalidated before each frame (`rebuild`).
All paths use the same modified core, build, target options and executor. The
control is not a separately built parent commit. No timing is a CI pass threshold.

Machine: Ryzen 7 5800H, 16 logical CPUs; AMD Radeon Graphics (RADV RENOIR),
Vulkan 1.4.318, Mesa RADV 25.2.8-0ubuntu0.24.04.4. Release builds; Vulkan explicitly
selected with `/usr/share/vulkan/icd.d/radeon_icd.json`. Single synchronous
128×128 offscreen target, RGBA/depth, no MSAA. No parallel GPU benchmark jobs.

A scene contains 256 or 2,048 translated/materialized occurrences of one shared
FaceSet triangle recipe. The phases keep the scene static, toggle one translation,
toggle one material's diffuse color, and toggle a coordinate in the shared recipe.
Each case has seven warmup frames and 31 timed calls. Three independent process
trials rotate path order. The raw data records both render-only and edit-plus-render timings. Rendering
includes synchronous submission and target publication/readback. The primary
table includes mutation and synchronous notifications; the extra output-vector
copies/equivalence check after each phase remain outside timing. First capture and
pipeline startup are excluded. Small triangles and tiny output favor CPU work;
these ratios cannot be generalized to complex CAD scenes or interactive windows.

Every phase compares final RGBA and depth exactly against a forced native rebuild
on a separate target. All 144 result rows passed; correctness of intermediate
patches is separately covered by the incremental test. The existing GL reference
test provides an additional bounded cross-renderer control.

The primary table includes editing, synchronous notification collection and
rendering. The correctness renderer/sensor is created only after each measured
phase and destroyed before the next, so it cannot inflate notification cost.
Both paths use the same mutation sequence. Values are milliseconds, median of
three per-trial medians; gain is current/retained.

| Executor | Occurrences | Edit | Current total ms | Retained total ms | Forced rebuild total ms | Gain |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| wgpu-vulkan | 256 | static | 0.233 | 0.218 | 1.160 | 1.07× |
| wgpu-vulkan | 256 | translation_one | 2.138 | 0.475 | 1.169 | 4.50× |
| wgpu-vulkan | 256 | diffuse_one | 2.186 | 0.503 | 1.151 | 4.34× |
| wgpu-vulkan | 256 | geometry_shared | 2.115 | 0.677 | 1.217 | 3.12× |
| wgpu-vulkan | 2048 | static | 0.234 | 0.221 | 9.596 | 1.06× |
| wgpu-vulkan | 2048 | translation_one | 16.960 | 1.404 | 7.790 | 12.08× |
| wgpu-vulkan | 2048 | diffuse_one | 15.644 | 1.400 | 7.876 | 11.17× |
| wgpu-vulkan | 2048 | geometry_shared | 16.046 | 5.380 | 8.356 | 2.98× |
| bgfx-vulkan | 256 | static | 1.007 | 1.237 | 2.099 | 0.81× |
| bgfx-vulkan | 256 | translation_one | 3.570 | 1.355 | 2.176 | 2.63× |
| bgfx-vulkan | 256 | diffuse_one | 3.664 | 1.577 | 2.273 | 2.32× |
| bgfx-vulkan | 256 | geometry_shared | 3.689 | 1.704 | 2.026 | 2.16× |
| bgfx-vulkan | 2048 | static | 7.861 | 7.318 | 19.247 | 1.07× |
| bgfx-vulkan | 2048 | translation_one | 26.157 | 12.845 | 18.770 | 2.04× |
| bgfx-vulkan | 2048 | diffuse_one | 23.881 | 10.908 | 17.089 | 2.19× |
| bgfx-vulkan | 2048 | geometry_shared | 27.546 | 15.212 | 17.197 | 1.81× |

Render-only medians at 2,048 occurrences (mutation/notification excluded):

| Executor | Edit | Current render ms | Retained render ms | Retained edit/notify ms |
| --- | --- | ---: | ---: | ---: |
| wgpu-vulkan | static | 0.234 | 0.221 | 0.000 |
| wgpu-vulkan | translation_one | 16.946 | 1.399 | 0.006 |
| wgpu-vulkan | diffuse_one | 15.632 | 1.394 | 0.006 |
| wgpu-vulkan | geometry_shared | 15.456 | 4.703 | 0.668 |
| bgfx-vulkan | static | 7.861 | 7.318 | 0.000 |
| bgfx-vulkan | translation_one | 26.138 | 12.821 | 0.024 |
| bgfx-vulkan | diffuse_one | 23.849 | 10.880 | 0.027 |
| bgfx-vulkan | geometry_shared | 26.755 | 14.253 | 0.824 |

The 256-occurrence BGFX static case regressed about 23% (1.007 to 1.237 ms).
At 2,048 occurrences static results were close to the current path, which already
has efficient reuse; this does not establish a general static speedup. Larger gains
are in edits, where native notifications avoid full capture and another
composition classification. All retained measured frames record zero composition
classifications. Current GPU static frames also record zero; changed frames record
one each. Forced native rebuild also uses the authoritative plan.

For every 31 measured retained translation/diffuse frames: zero captures, zero
plan builds, zero transport builds, 31 native updates, 31 patched transport
commands, and zero copied vertices/indices. For shared geometry: zero captures,
31 plans and transport rebuilds, 63,488 affected commands and 190,464 copied
vertices/indices at 2,048 occurrences. Those are bridge-copy counters, not GPU
traffic counters.

The wgpu resource-cache telemetry shows remaining bandwidth costs. At 2,048
occurrences over 31 frames, translation uploaded 6,094,848 bytes in both paths;
diffuse uploaded 4,960 bytes in current versus 5,079,040 in retained; geometry
uploaded 6,115,432 in current versus 11,189,512 in retained. The adapter's separate
material slot per draw explains the diffuse amplification. Static uploads were
zero in both. These counters are cache telemetry, not total bus traffic. BGFX
currently does not expose this telemetry, so missing values are not zero uploads.
The CPU latency win does not establish improved GPU bandwidth.

Remaining work includes preserving/deduplicating native material/resource identity
in the transport, forwarding resource/subrange deltas instead of full geometry
conversion, and qualifying broader rendering profiles. Capture and planning have
one authority throughout this implementation; no duplicate renderer is used to
recover missing semantics.

Reproduce with separate Release configurations, COIN_BUILD_RENDER=ON,
COIN_BUILD_TESTS=ON, and COIN_RENDER_BACKEND=RUST_BRIDGE or BGFX:

```sh
cmake --build <build> --target CoinRenderRetainedBenchmark
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json <build>/bin/CoinRenderRetainedBenchmark current 2048 31
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json <build>/bin/CoinRenderRetainedBenchmark retained 2048 31
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json <build>/bin/CoinRenderRetainedBenchmark rebuild 2048 31
```

Repeat three times, rotating mode order; repeat at 256 occurrences. Each command
runs all four phases in its own process. [Raw JSONL](coin-render-retained-benchmark.jsonl)
contains render medians/p90s, mutation and total medians, equality checks,
counters and trial numbers.

Validation: four retained tests pass on each of RECORDING, wgpu/Vulkan and
BGFX/Vulkan, plus four selected existing RECORDING regressions and 30 sensor/path auditor tests. The expanded
incremental test covers inherited root state, mixed notifications, shared-state
fallback, repeated auditor snapshots, invalid material rejection and recovery.
