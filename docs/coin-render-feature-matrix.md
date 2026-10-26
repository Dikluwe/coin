# Current scope and qualification index

This English index consolidates implementation contracts and qualifications while
leaving the complete dated R/C/B/V ledger in the
[local companion archive](coin-render-evidence.md), under
`integrated/docs/coin-render-feature-matrix.md` and
`historical/docs/coin-render-feature-matrix.md`. The full historical ledger was
archived, not translated line by line. Its individual revisions, failures,
retests, baseline comparisons and unqualified cross-products remain authoritative
evidence for those historical runs. This index is not a new GPU qualification of
the contribution HEAD.

Feature implementation, available hardware and qualified profile are separate.
Safe rejection does not implement a missing capability. A passing cell qualifies
only its exercised input/profile, never every resource/state combination. Skips,
CPU-only checks, artifact generation and successful present return codes do not
qualify physical rendering. Historic stage closure is project tracking, not proof
that open failures disappeared.

## Recorded Linux cells

The recurring seven-cell historical matrix was:

| Executor | AMD | NVIDIA |
| --- | --- | --- |
| BGFX | Vulkan, OpenGL | Vulkan, OpenGL |
| Rust/wgpu | Vulkan, OpenGL | Vulkan; OpenGL unavailable |

Recent manager profiles used AMD Renoir/Mesa **25.2.8** and NVIDIA RTX 3060 Laptop,
driver **615.71.09**. Some NVIDIA/OpenGL controls used private Xorg with NVIDIA as
primary, without PRIME, headless and without physical scanout. Those controls do
not qualify affected hybrid/offload recreation. Earlier campaigns use their own
recorded drivers and references, including Mesa/llvmpipe where explicitly stated.

## Resource and combination profiles

| IDs | Implemented/qualified scope | Preserved limits |
| --- | --- | --- |
| P01/F03 | Up to eight captured clipping planes; common triangle/line/point contract | General edge/MSAA/device/host matrix remains per-case |
| P02/F04, C04/C05 | Native styles, recoverable contours, concavity/per-occurrence attributes, original-face offset; selected alpha/viewport/UV combinations | No blanket subclass/custom GL, raster-junction or mixed-binding parity |
| P04/P05/P06 | External/empty viewports, paired bindings, Gouraud/fog and captured depth ranges | Not all 49 mixed material/normal binding pairs, shapes, driver coverage or custom modes |
| P07/P08 | Projective UV, eight-unit combine/strokes, explicit sampling options | Native CoinGL four-stage limitation separated from eight-stage analytic oracle; native numeric/quality caveats remain |
| P09/P10, C06 | Eleven Coin transparency modes; peeling1..8/weighted OIT and selected explicit depth/MSAA/shadows | Weighted is approximate, no automatic substitution; layer budgets/format restrictions apply |
| P11 | Typed immutable options and versioned capability/selection API | Hardware facts/implementation/qualification are independent; probe is not window presentation |
| P12/P13/P14 | Logical RTT DAG, owner/device/generations, transactional publication, tickets/multiple targets/recovery | Nominal debits are not total memory; per-backend views/registry/thread rules remain |
| C07/C08 | Native bitmap text/image/markers with selected sorting/fog/alpha-test/shadow/RTT/window profiles | Native B21 raster reference and unlisted combinations remain distinct from portable controls |
| C17/C21 | Selected spatial texture/bump/shadow-camera and multi-map combinations | Qualification is by individual subcase, not all C17/C21 combinations |
| R22 | Portable v1 typed fragment AST, bounded signature/source/transport; selected2D/composition/RTT | No arbitrary shader translation, vertex/discard/compute/depth authoring, SAMPLE3D/cube or CPU shader execution |
| R23 | Stored linear/sRGB volume atlas, integer axes1..256, NPOT spatial mips, selected mixed resources | No compression/HDR/depth/spatial anisotropy>1/FRACTURE; C14 quality transitions stay bounded |
| R24 | Stored six-face POT linear/sRGB cubes1..2048 with per-face mips | No seamless/NPOT/compressed/HDR/depth cube or authored SAMPLECUBE |
| R25 | Six-face cubemap RTT, STAGED/DIRECT GPU and public RECORDING STAGED1× | CPU DIRECT/MSAA/window/async/shadows/bump remain rejected; no public cube export |
| R26 | Legacy opaque bump1..8 lights; selected stored/RTT bases, directional shadows, MSAA/window | CPU math/capture only; no translucent bump/PBR/arbitrary same-draw shader modules |
| R27 | Selected linear RGBA8 MSAA1×/2×/4×, resolve and synchronous/async tickets | Exact sample capability required; other formats/platforms/cross-products separate |
| R28 | Action/manager multipass1..16, single capture, GPU average color/last-pass depth, bounded resources | Additional native callback/options under their own profiles; no repeated author side effects per jitter sample |
| R33 | Recorded Linux VRML97 visual profiles | Not event/application execution, arbitrary graph injection or every format/field combination |
| R34/R36 | Recorded portable options and offscreen consumer | Runtime GL callbacks/extra formats/platforms remain separate |
| R30 | Linux mono RGB manager, technical modes, authored BG/base/FG layers | No persistent color LOAD without effective clear owner, color-index or generic framebuffer overlays |
| R29 | ANAGLYPH/interleaved rows/columns from complete GPU eye results | No native LEFT/RIGHT buffers for SEPARATE_OUTPUT/QUAD_BUFFER; no complete hardware-stereo qualification |
| R35/C25 | Native BOX/LINE highlights per eye and technical mode with representative resource combinations | No arbitrary GL-action runtime/HDR/generic async manager; base capabilities still gate highlight |
| R19/B10 | Bounded instancing/reuse optimizations and benchmark profiles | Aggregate B10 remains open; local timing is not universal speedup |
| R20/R21 | Local host-integration reports preserved in archive | Host patches/macros excluded from this generic source submission; no blanket workbench certification |

Normative details are linked from the [maintainer guide](coin-render.md). Exact
C-subcase lists and B-fix causal/baseline evidence are preserved in the original
matrix, so archiving its long narration does not erase open work.

Selected recent historical campaign results (not additive into one global gate):

| Profile | Recorded result and boundary |
| --- | --- |
| R22 | CPU39/39; GPU105/105 new and286/286 regression; Rust59 pass/4 ignored |
| R23 sRGB volume | CPU18/18 new,48/48 regression; GPU164/164 new,132/132 regression; Rust59/4 ignored |
| R25 public CPU | RECORDING STAGED1×33/33, including sixth-face failure and explicit same-size-resize root-OOM recovery |
| R26 completion | CPU1 new/46 regression; GPU103 new/221 valid regression; Rust64 pass/11 ignored; no final skips/timeouts |
| R27 async completion | GPU267/267 new,204/204 regression; CPU34/34; Rust64 pass/11 ignored; seven directed mutations detected |
| R28 | CPU60/60; wgpu99/99; joint231 entries after fixture reconciliation; fixture21/21; shutdown6/6; cold windows18/18; ASAN3/3 |
| R30 mono | Code/readback/profile qualifications retained, but final NVIDIA/wgpu WindowLayers1 physical observation failed under B95 |
| R29 stereo |350/350 profiles (343 GPU+7 Core), nine additional CPU controls;375 distinct regression controls; historical mono B95 recurrence remains FAIL |
| R35 highlight |462/462 new (434 GPU+28 CPU),376/376 proportional regressions; Rust59 pass/4 preexisting ignored;14 window processes passed without closing prior B95 |

These counts describe specific historical revisions/tests, not unique feature
counts or a quality percentage. For R29/R35 old/new shared public gates, baseline
failures were functional refusals/assertions, not incompatible loader/ABI evidence.
Experimental bridge evolution reached **ABI70/FrameView600**; R33 used68/544,
R34 used69/560, R28 used65/544. Rebuild each baseline with its matching headers/
bridge/shaders. CoinRender implementation uses C++17 privately; public Coin header/
consumer C++11 compatibility remains separately tested. Symbol/layout comparisons
recorded on Linux do not qualify every compiler/platform.

## External and historical failures

| ID | Status and implication |
| --- | --- |
| B34 | **External, reproduced, open.** Standalone Coin/BGFX-free native EGL/X11 reproducer: AMD4/4 and NVIDIA-primary/no-PRIME4/4 passed; NVIDIA offload0/4 (two SIGSEGV, one180s timeout, one RGB failure). Audit observed NVIDIA access to closed XCB generation during eglMakeCurrent despite a valid new connection. Affected hybrid NVIDIA/OpenGL surface recreation remains unqualified. No retained-runtime/vendor workaround/retry was incorporated. Primary headless controls neither qualify scanout nor prove PRIME alone causal. |
| B94 | **External WSI acquisition limitation open; local recovery defects fixed separately.** AMD/RADV Xlib FIFO independent Vulkan/GLX reproducer returned VK_TIMEOUT on fourth acquire after three correct readbacks/fences, window viewable, validation zero. DPMS controls changed timing but cannot retroactively prove original cause. Local Action recovery guard/fence-Ok(false) handling and camera-patch validation were corrected; no larger timeout/retry. |
| B95 | **External physical stale presentation reproduced, open.** Independent Vulkan/Xlib two-image IMMEDIATE and stock wgpu FIFO controls retained old window image for original5s despite current readback/completion/successful API presentation. WSI/driver/compositor responsibility remains unresolved; wgpu/HAL remains in FIFO scope. These are distinct from B34/B94. Affected NVIDIA/Vulkan BGFX two-image IMMEDIATE and wgpu FIFO physical presentation remains unqualified. No extra apply/present/wait/triple-buffer workaround. |
| B03.2/B34.1/B34.2/C21.12/B87 | Five historical failures did not reproduce in18 fresh target processes in the recorded revalidation; no skips/timeouts/failures. Preserve earlier failures without asserting an established common cause or causal fix. Recurrence reopens its own investigation. |
| B09/B17/B18/B20/B21/B29 and aggregate B10 | Preserve subcase diagnoses and strict native differences in original ledger. A portable passing profile does not convert an unresolved native comparison into parity. |

Reproducers remain at
[B34 native X11](../testsuite/reproducers/b34-native-x11/),
[B94 Vulkan WSI](../testsuite/reproducers/b94-vulkan-wsi/),
[B95 Vulkan](../testsuite/reproducers/b95-vulkan-stale-present/), and
[B95 stock wgpu](../testsuite/reproducers/b95-wgpu-stale-present/).
The standalone B95 physical-failure return **3** differs from validation/teardown
invalid-trial return **4** and from a watchdog timeout. Observe original deadlines,
new-process/cold-cache policy and diagnostics; do not replace failed observations
with opportunistic reruns.

## Platform qualification remains historical

| IDs | Platform/profile | Recorded scope and gaps |
| --- | --- | --- |
| V01–V04 | Linux GPU cells above | Qualified subsets only; wgpu NVIDIA/OpenGL unavailable, AMD/OpenGL window COPY_SRC unavailable in recorded profile |
| V05 | Physical Intel Linux | General cell pending |
| V06 | BGFX Windows D3D12/Vulkan/OpenGL | Historical clean aggregate313 PASS/1 SKIP/1 FAIL, **not313 per API**; B02/B07/DPI/driver/host/rest functional matrix open |
| V07 | wgpu Windows D3D12/Vulkan/OpenGL | Historical clean aggregate218 PASS/1 SKIP/0 FAIL; not blanket current or all-API qualification |
| V08 | Windows RECORDING/OFF | Historical131/50/0 and OFF77/0/0; skips are not GPU qualification |
| V09 | Android x86_64 GLES AVD |23/10 recorded cube1×/2×/4×,40k4×/million1×, capture/pause/resume/rotation/BACK profile; emulator8GiB temporary configuration does not qualify ARM/G7/Vulkan |
| V10/V11 | Android Vulkan/physical ARM64 | AVD Vulkan driver failure; full physical APK/input/lifecycle/loss/performance qualification remains |
| V12 | macOS AppKit/Metal | Selection/CAMetalLayer code exists; native build/link/SDK/render/resize/peers/recovery/performance pending |
| V13/V14 | Wayland | wgpu Weston/recorded scale subsets; physical compositor/fractional scale/monitor broader matrix open. BGFX connector lacks Wayland mechanism |
| V15 | Dawn/wgpu-native | Prototypes, not equivalents of qualified BGFX/Rust profiles |

Windows historical clean runs mainly used revision
`2620d12da6064cbfc773ac8acfa05adbd7c1b1c9`, RECORDING repeat
`eb13545855503661e79c6c660e09aa57081142fa`, GTX1060/driver581.08.
Integrating those sources does not rerun their matrix. This contribution
consolidation newly qualifies **none** of Windows DX12/OpenGL/Vulkan, Android
physical, macOS Metal or additional GPU profiles.

Browser WebGPU is a separate small capture/instancing demo. TCL40k recorded
**8.50 FPS median, 17.02 s preparation**. TCL million took **365.68s**, produced a clear image and is
**not qualified**. G7 million has no valid FPS. No ratio or extrapolated throughput
may be inferred; see [browser example](../examples/coinrender/webgpu/README.md).

## Updating a qualification

Record ID/subcase, prior/new implementation and evidence status separately,
Coin nodes/states and expected behavior, executor/API/platform/GPU/driver/target,
source/library/shader/dependency hashes, fixture/command/oracle, PASS/FAIL/SKIP/
not-run, rollback and recovery, precision/performance limits, evidence location and
next open case. Keep aggregate IDs open until all applicable subcases close.
Do not count environment failure as a proven code defect or skipped comparisons
as passes. Serialize GPU tests and keep original deadlines/tolerances. SSAO,
compute/culling, expanded indirect, dynamic executor selection, ray tracing and
PBR are separate goals requiring their own contracts and integrated evidence.
