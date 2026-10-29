# Retained geometry/material follow-up — 2026-10-27

Branch: `codex/coinrender-retained-integration`. This follows the
[first measurements](coin-render-retained-benchmark.md), whose raw data is
preserved. Both versions are local connector revisions on this branch, not
separately committed/published releases.

## What changed

- Committed native geometry deltas patch positions and indices in their existing
  owned transport ranges. Positions/indices whose bits/values did not change are
  not copied. No frame, camera, material or render-state table is recreated.
- Native generation/membership, exact delta base revision, layout, finite positions
  and index bounds qualify reuse. Missed deltas/structural changes take the full
  path; malformed deltas reject before publication. The fork's geometry updater
  already requires matching layouts. Topology changes still require one fresh
  capture/transport; they cannot safely keep stale spans.
- A changed native plan is consumed as the authoritative schedule over existing
  draws. Its membership/order are checked without invoking another planner or
  composition classifier. Geometry still invalidates the fork's plan domain, so
  the native planner runs once per geometry edit, not zero times.
- Equal complete material values share slots. Diffuse edits split shared slots
  before writing; uniquely owned slots update in place and vacant slots can be
  recycled. Vertex material indices follow that ownership change. This avoids
  one material entry per occurrence without treating object IDs as cache keys.
- BGFX qualifies uniform opaque untextured BASE_COLOR for its existing instance
  shader, setting the existing PHONG flag to zero. CPU and GPU controls verify
  unlit values, camera patches, depth and general-route equivalence.
- BGFX compares owned packed mesh/index contents before reusing those GPU
  buffers, then uploads contiguous changed instance records only. Released CPU
  geometry, changed layouts/mesh/index contents, invalid buffers or budget limits
  retain the ordinary fallback. No guessed revision proves mesh equality.

Capture, native resource transactions and native planning remain the sole scene
semantics authorities. Executor lowering here is GPU packing, not another scene
traversal or planner. GPU geometry uploads may still rebuild/repack buffers;
eliminating transport reconstruction is not a claim of universal subrange GPU
geometry uploads.

## Method and results

Same machine/profile as the first report: Ryzen 7 5800H, AMD RADV RENOIR,
Mesa 25.2.8, Vulkan, Release, synchronous 128×128 RGBA/depth, one shared FaceSet
triangle across 256 or 2,048 occurrences. Seven warmups, 31 measured edits/frames,
three independent process trials, path order rotated, GPU jobs serialized.
Numbers include mutation, synchronous notifications and render/publication.
Additional output-vector copying and control rendering are outside timing. No
frequency pinning or statistical significance claim; these are synthetic CPU-heavy
cases, not representative qualification of all CAD scenes or windows.

`current` is CoinRenderAction in the same newly built binary; `retained` uses the
new connector; `rebuild` forces that connector to rebuild every frame. General
executor changes can affect either path. The control renderer/sensor is absent
from measured mutations. Every phase's final RGBA and depth match forced native
rebuilds exactly: **144/144 main rows and 24/24 ablation rows**. Independent BGFX
GPU tests also compare instancing with the general encoder, including depth.

Times are the median of three per-trial medians, in milliseconds.

| Executor | Occurrences | Edit | Current | Retained | Forced rebuild | Current/retained |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| wgpu-vulkan | 256 | static | 0.224 | 0.220 | 1.169 | 1.02× |
| wgpu-vulkan | 256 | translation_one | 2.114 | 0.453 | 1.179 | 4.66× |
| wgpu-vulkan | 256 | diffuse_one | 2.110 | 0.491 | 1.180 | 4.30× |
| wgpu-vulkan | 256 | geometry_shared | 2.112 | 0.559 | 1.233 | 3.78× |
| wgpu-vulkan | 2048 | static | 0.236 | 0.236 | 10.217 | 1.00× |
| wgpu-vulkan | 2048 | translation_one | 17.038 | 1.404 | 8.120 | 12.13× |
| wgpu-vulkan | 2048 | diffuse_one | 16.275 | 1.431 | 8.030 | 11.37× |
| wgpu-vulkan | 2048 | geometry_shared | 16.366 | 2.871 | 8.739 | 5.70× |
| bgfx-vulkan | 256 | static | 1.045 | 0.312 | 1.308 | 3.35× |
| bgfx-vulkan | 256 | translation_one | 3.670 | 0.448 | 1.340 | 8.18× |
| bgfx-vulkan | 256 | diffuse_one | 3.848 | 0.485 | 1.326 | 7.93× |
| bgfx-vulkan | 256 | geometry_shared | 3.823 | 0.627 | 1.327 | 6.10× |
| bgfx-vulkan | 2048 | static | 7.872 | 0.331 | 9.980 | 23.80× |
| bgfx-vulkan | 2048 | translation_one | 30.930 | 1.319 | 8.219 | 23.45× |
| bgfx-vulkan | 2048 | diffuse_one | 29.286 | 1.374 | 8.235 | 21.32× |
| bgfx-vulkan | 2048 | geometry_shared | 29.027 | 2.726 | 8.818 | 10.65× |

Comparison with the previous retained connector (separate prior measurement,
so small differences may be run-to-run variation):

| Executor | Occurrences | Edit | Previous retained ms | New retained ms | Previous/new |
| --- | ---: | --- | ---: | ---: | ---: |
| wgpu-vulkan | 256 | static | 0.218 | 0.220 | 0.99× |
| wgpu-vulkan | 256 | translation_one | 0.475 | 0.453 | 1.05× |
| wgpu-vulkan | 256 | diffuse_one | 0.503 | 0.491 | 1.03× |
| wgpu-vulkan | 256 | geometry_shared | 0.677 | 0.559 | 1.21× |
| wgpu-vulkan | 2048 | static | 0.221 | 0.236 | 0.94× |
| wgpu-vulkan | 2048 | translation_one | 1.404 | 1.404 | 1.00× |
| wgpu-vulkan | 2048 | diffuse_one | 1.400 | 1.431 | 0.98× |
| wgpu-vulkan | 2048 | geometry_shared | 5.380 | 2.871 | 1.87× |
| bgfx-vulkan | 256 | static | 1.237 | 0.312 | 3.97× |
| bgfx-vulkan | 256 | translation_one | 1.355 | 0.448 | 3.02× |
| bgfx-vulkan | 256 | diffuse_one | 1.577 | 0.485 | 3.25× |
| bgfx-vulkan | 256 | geometry_shared | 1.704 | 0.627 | 2.72× |
| bgfx-vulkan | 2048 | static | 7.318 | 0.331 | 22.12× |
| bgfx-vulkan | 2048 | translation_one | 12.845 | 1.319 | 9.74× |
| bgfx-vulkan | 2048 | diffuse_one | 10.908 | 1.374 | 7.94× |
| bgfx-vulkan | 2048 | geometry_shared | 15.212 | 2.726 | 5.58× |

wgpu diffuse latency at 2,048 occurrences is roughly unchanged/slightly worse
(1.400 → 1.431 ms), despite the upload reduction. wgpu static also did not improve
(0.221 → 0.236 ms). The large geometry/BGFX improvements do not imply every phase
became faster.

## Transport and upload evidence

For each 31-frame retained geometry phase at 2,048 occurrences: zero captures,
**zero transport rebuilds**, 31 native plan builds, 31 geometry transport patches,
63,488 modified vertex positions and zero index copies. Previously this was 31
transport rebuilds plus 190,464 complete vertex copies and 190,464 index copies.
Geometry that changes multiple positions/indices copies those changed values;
shared native geometry still has per-occurrence transport spans. This does not
establish constant work independent of occurrence count.

Static/translation/diffuse phases retain zero captures/plans/transport rebuilds.
Every measured retained phase records zero composition classifications. Materials
occupy one slot initially and two after one occurrence changes color, rather
than 2,048 slots. Repeated-edit tests ensure slots do not grow on every edit.

wgpu resource-cache telemetry, bytes summed over 31 frames at 2,048 occurrences:

| Edit | Previous retained bytes | New retained bytes | Current bytes |
| --- | ---: | ---: | ---: |
| static | 0 | 0 | 0 |
| translation_one | 6094848 | 6094848 | 6094848 |
| diffuse_one | 5079040 | 4960 | 4960 |
| geometry_shared | 11189512 | 6115432 | 6115432 |

Diffuse uploads fell **1,024×** (99.90%), matching the current path's cache bytes.
These are resource-cache counters, not total bus traffic. BGFX has no equivalent
public telemetry; its phase trace verifies `ranges=1 instances=1
uploaded_bytes=160 reused_mesh=1` for a local translation or diffuse edit.
The shader instance record includes material and matrix values. A complete
2,048-instance buffer would contain 327,680 bytes; the new patch does not send
that complete buffer for one changed instance. The previous BGFX general route
had a different vertex-material patch format, so that full-buffer comparison is
not a measured old/new bandwidth ratio.

## BGFX static investigation

Before the change both paths reported `resource_cache_hit=1` on static frames,
with 256 ordinary draws. No extra retained capture, planning or geometry upload
was found. BASE_COLOR was refused by the instancing qualifier even though the
shader already supports its unlit flag. The new retained route packs one ordered
instance batch (one draw), with identical output under the controls.

A same-binary ablation disables only instancing via
`COIN_BGFX_DISABLE_INSTANCING=1`, at 256 occurrences (three trials):

| Path | Instancing enabled ms | Instancing disabled ms |
| --- | ---: | ---: |
| current | 1.045 | 1.177 |
| retained | 0.312 | 1.262 |

The current capture retains `explicitDepthMask=15`, which keeps its existing
conservative instancing rejection. The native retained capture supplies resolved
effective depth values and qualifies here. The implementation does not clear
explicit-depth metadata in the current path to manufacture eligibility.

This isolates a useful packing/submission bottleneck. It does not prove that
all of the earlier 23% relative regression had one cause: the disabled-instance
retained route is still somewhat slower in this rerun, and CPU/GPU scheduling
and readback latency vary. With instancing enabled, retained static latency is
now lower than both that ablation and the measured current path.

## Validation and reproduction

Four retained tests pass on each executor (RECORDING, wgpu/Vulkan, BGFX/Vulkan),
four existing RECORDING regressions pass, and four BGFX regressions pass:
CoreTest, InstancedPayloadTest, InstancedStressTest, InstancingTest. The expanded
core/GPU tests cover unlit flag/color equivalence, camera/depth, minimal instance
ranges, rejected changed meshes/indices and released CPU storage. Connector tests
cover repeated edits, copy-on-write isolation, bounded material slot growth,
invalid geometry/material publication, topology fallback and recovery.
A pre-existing CoreTest diagnostic expectation was aligned with the bump
validator's already-current text; rejection and packet-preservation assertions
remain in place.

Reproduce using the benchmark commands and build flags in the
[first report](coin-render-retained-benchmark.md), with the updated benchmark.
Rotate current/retained/rebuild process order across three trials at both sizes.
For the static ablation repeat current/retained at 256 with
`COIN_BGFX_DISABLE_INSTANCING=1` (the executable still runs every phase).
Do not run GPU tests/builds alongside timed jobs. Phase tracing is disabled during
timing; use `COIN_RENDER_TRACE_PHASES=1` separately for packing/upload evidence.

[Main raw JSONL](coin-render-retained-benchmark-v2.jsonl) ·
[Static ablation raw JSONL](coin-render-retained-bgfx-static-ablation-v2.jsonl).
