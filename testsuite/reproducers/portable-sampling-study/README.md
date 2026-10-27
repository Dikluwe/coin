> This revision uses the public per-target sampling API. The historical commands
> below require sources/runtimes frozen at `43f00b0e` or earlier:
> `COIN_SAMPLING_STUDY` no longer selects the rendering policy. The runners
> reject incompatible runtimes to prevent mislabeled measurements.
> For this revision, use `../sampling-api/run.py` and
> `docs/coin-render-texture-sampling-api.md`. Independent EGL probes remain valid.

# Portable sampling — isolated laboratory, 2026-10-07

Historical branch `codex/coin-portable-sampling-study`, base
`f94a6c2898582d6dfd5796771392c38a541f4fb2`. The original Coin implementation was
unchanged and the gates were not relaxed.

Dated reports mentioned below are preserved at their original paths in the
companion archive; see the [evidence guide](../../../docs/coin-render-evidence.md).
These measurements do not qualify the current contribution revision.

`COIN_SAMPLING_STUDY` was an experimental selector, read once per process:

- `native`/unset: native sampler.
- `nearest`: nearest/trilinear reconstruction through fetch, filter 2 only.
- `fetch`: isotropic nearest/bilinear/trilinear reconstruction.
- `fine`: center at mip `floor(LOD)`, one nearest/trilinear sample for POT;
  NPOT uses both centers. Isotropic filter 2 only, with linear magnification.
- `fine_uniform`: the same algorithm, with mip dimensions derived from a uniform
  containing the size **of the bound resource**, including RTT.
- `center`: explicit LOD, floor texel selection, centers with the native sampler
  and explicit blending of two mips; filter 2 only. Magnification remains linear.

All modes retain native anisotropy above 1, typed formats, sRGB/HDR/BC3, disabled
state and RTT matrix/flip. They do not promise identical pixels for arbitrary
geometry/GPUs: rasterization, interpolation and derivatives still have limits.
BGFX GLSL 330 uses ordinary derivatives; Naga 24 also falls back from fine to
ordinary derivatives where GLSL lacks support. The temporary representation in
`tex_params.x` is neither a public contract nor a final API proposal.

## Reproduction

Configure Release builds with CMake, `COIN_BUILD_RENDER=ON`,
`COIN_BUILD_TESTS=ON`, `COIN_BUILD_RENDER_BENCHMARKS=ON`,
`COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON` and
`COIN_RENDER_BACKEND=RUST_BRIDGE`/`BGFX`. BGFX requires an installed shaderc and
package. Use `build-wgpu` and `build-bgfx` under a **permanent** artifact directory.

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch --extra
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fetch --alpha --output-name quality-alpha
c++ -O2 -std=c++17 testsuite/reproducers/portable-sampling-study/gpu_benchmark.cpp -o "$study_artifacts/gpu-benchmark-optimized" -lEGL -lOpenGL
python3 testsuite/reproducers/portable-sampling-study/benchmark.py --artifacts "$study_artifacts" --kind gpu --optimized
```

The initial phase uses a shader with integer modulo for wrapping. Its source,
patch and binary must not be mixed with the optimized phase. Manifests record
executable hashes. City baselines use the earlier coin-render branch build;
window runs accept external scenes and record exact paths. `benchmark.py` uses
paths from the original PC and must be adapted on another system.

GPU/EGL: RGBA8 1280×720 target, 1024² checker, 1/4/8 units, 30 warmups,
five groups of 60 draws per process, interleaved ordering and GL_TIME_ELAPSED
without readback. This measures GPU work, not presentation. In the optimized
phase, mode 3 uses centering for nearest and native sampling for linear; mode 4
uses fetch with comparison-based wrapping. Window/wgpu Vulkan: 1280×720,
45 warmups, 150 frames and CSV samples; CPU render/present call time without
GPU timestamps or screen latency. Cities use perspective/viewAll without
textures. The textured scene covers the target with an orthographic plane,
128² texture and UV0..96, using 1/4/8 units. Do not compare these numbers directly
with the Android/AVD 2400×1080 measurements.

Full tests can return 1 even when CPU/GPU results agree: the AMD CoinGL reference
diverges. FAIL remains FAIL; sub-contract extractions are explicit. CPU/CoinGL
line and OpenGL sphere/UV failures are retained, not bypassed. See the archived
report `docs/coin-render-portable-sampling-study-20261007.md`.

`boundary.cpp` builds with `-lEGL -lOpenGL`, accepts an output prefix and checks
4,096 pixels per mode at mip 3. Run it separately under each EGL vendor to record
native/center/fetch results and actual GPU identity.

## Single-sample candidates — 2026-10-08

`base_probe.cpp` and `run_base.py` are independent EGL/OpenGL probes without Coin.
`base`/`base_uniform` are names exclusive to those probes. `fine`/`fine_uniform`
were also integrated into the bridge during this isolated continuation. Base
size alone does not safely match POT at coarse mips on AMD; `fine` centers at
mip `floor(LOD)`. The variants use both centers for NPOT. See the archived
`docs/coin-render-base-sampling-counterexample-20261008.md`.
The `--derived` control computes LOD8 from the original UVs and validates every
pixel. Performance and magnification were not measured in that probe; they remain
subsequent controls.

## Integrated fine/fine_uniform campaign

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --deep --output-name quality-deep
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --rtt-study --output-name quality-direct
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform --viewport-study --output-name quality-viewport
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,center,fine,fine_uniform
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes fine,fine_uniform --extra --output-name quality-extra
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes fine_uniform --resources --output-name quality-resources
c++ -O2 -std=c++17 testsuite/reproducers/portable-sampling-study/gpu_benchmark.cpp -o "$study_artifacts/gpu-benchmark-fine" -lEGL -lOpenGL
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind gpu
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind window
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind city --backends wgpu
python3 testsuite/reproducers/portable-sampling-study/summarize_fine.py --artifacts "$study_artifacts"
```

`--deep`: 162 cases per process, deep/fractional mips, magnification, wrapping,
clamping, unit 0/7/both, square/rectangular POT, NPOT and RGBA8/sRGB/HDR/BC3.
`--rtt-study`: 48 explicit private filter-2 consumers with direct RGBA8/RGBA16F
resources and resize. Public SoSceneTexture2 uses linear/trilinear (filters 1/3)
and does not activate fine; the public RTT gate remains separate and unchanged.
GPU/EGL modes 0/3/5/6 are native/center/fine/fine_uniform, with 30 warmups and
five groups of 60 draws. Window runs use the production baseline, study native
and three candidates in forward and reverse order; 45 warmups and 150 measured
frames, wgpu/BGFX Vulkan on AMD/NVIDIA. `--viewport-study` repeats 36 POT/NPOT
cases with a 24×18 viewport at origin (4,5), comparing the physical interior
without changing the gates. CSVs include warmups; aggregation excludes
`warmup=1` and measures `render_present_ms`. Scenes use POT128² and NPOT129×127,
with 1/8 units. EGL reuses a 1024² image across units: this is a warm cache,
not streaming or general texture usage. Do not run GPU workloads concurrently.
Scene and baseline paths are local: `scenes-fine`, wgpu/BGFX builds and the EGL
executable must exist before the campaign.

DPMS is recorded before and after every window benchmark. An Off monitor is
reactivated with `xset dpms force on`, without changing preferences or unlocking
the session. Measurements where the monitor did not remain On are excluded from
aggregation; the earlier DPMS-Off trial is preserved separately. `--resume`
avoids repeating completed logs after a setup interruption.

## Native-path separation — 2026-10-08

The laboratory compiles the native path without sampling helpers or the 128-byte
tail. Wgpu binds the original 3040-byte prefix (stride 3072 with 256-byte
alignment); the experimental profile uses 3168/3328. Untextured instancing always
uses the native profile, including under fine_uniform. BGFX compiles separate
native/experimental fragments, neither creates nor sends the extra native-path
uniform, and removes the tail from draws. Only fine_uniform queries bound sizes.
The selector remains private and fixed per process; this was not yet a public API.

```sh
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,fine_uniform --resources --output-name quality-resources
python3 testsuite/reproducers/portable-sampling-study/run.py --artifacts "$study_artifacts" --modes native,fine_uniform --shadow --output-name quality-shadow
python3 testsuite/reproducers/portable-sampling-study/benchmark_fine.py --artifacts "$study_artifacts" --kind window --modes baseline,original-native,native,fine_uniform
python3 testsuite/reproducers/portable-sampling-study/investigate_native.py --artifacts "$study_artifacts" --phase before
python3 testsuite/reproducers/portable-sampling-study/investigate_native.py --artifacts "$study_artifacts" --phase after --candidate-build "$study_artifacts/build-wgpu"
python3 testsuite/reproducers/portable-sampling-study/analyze_native.py --artifacts "$study_artifacts"
```

Before rebuilding, copy the executable and its libraries to `original-study/bin`
and `original-study/lib`, recording hashes/revision. The runner sets
LD_LIBRARY_PATH for each child; otherwise the copied executable's absolute
RUNPATH loads the rebuilt library. The baseline uses the production builds
recorded in the commands. The million-instance control runs all six permutations
of three modes (18 processes per phase), 45 warmups/150 frames and NVIDIA
telemetry. `--resume` preserves the DPMS/error-rejected trial in
`rejected-monitor-off` and repeats only invalid controls.

COIN_SAMPLING_AUDIT prints hashes/bytes for uniforms, instances, draws and units.
For the window GPU probe, also enable COIN_WGPU_TRACE_PHASES=1 and
COIN_WGPU_GPU_TIMESTAMPS=1. It requires TIMESTAMP_QUERY and
TIMESTAMP_QUERY_INSIDE_ENCODERS; COIN_SAMPLING_AUDIT_GPU measures the encoder
section. It resolves/copies timestamps and waits synchronously: use separate
processes and exclude these measurements from normal render/present medians.
This is neither screen-latency measurement nor an asynchronous production profiler.

Archived report: `docs/coin-render-native-sampling-path-study-20261008.md`.

## Correction of 28 failures — 2026-10-08

The continuation preserves tolerances and separates geometry/depth corrections
from the **external Mesa workaround**, retained in [mesa/README.md](mesa/README.md).
The individual ledger and A/B controls are in the archived
`docs/coin-render-failure-closure-20261008.md`. The original Coin implementation,
the main coin-render branch and the installed driver were unchanged by that study.
