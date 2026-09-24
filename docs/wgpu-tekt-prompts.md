# SoWgpu experimental rewrite: causal prompts

This document records the prompts that govern the experimental rewrite on
`tekt/sowgpu-render-action`. They are study and traceability artifacts, not a
required source directory layout.

The Open Inventor model is authoritative:

- rendering starts in an `SoAction` and follows its traversal rules;
- scene state continues to use `SoState`, elements, bundles and primitive
  callbacks;
- calculations reuse Coin `Sb*` types and established Coin facilities;
- the WebGPU module remains opt-in and separate from `libCoin`;
- no public Coin 4 ABI change is allowed.

## General prompt

Improve the experimental `SoWgpuRenderAction` as a product while preserving
Open Inventor behavior and Coin conventions. Use Core, Shell, Infra and Wiring
as planning responsibilities, not as mandatory directories or libraries.

**Core** contains Coin-native mechanical transformations needed between scene
capture and the WebGPU connector, and processing of connector results. It may
use Coin types but does not control traversal, submit GPU work or write
user-facing prose.

**Shell** owns language-facing behavior: structured diagnostics, error
messages, textual configuration, logs, frame dumps and profiling output.

**Infra** is the concrete WebGPU connector: private C ABI, Rust bridge, device,
queue, resources, pipelines, surfaces, command submission, readback and device
loss. It does not traverse the Coin scene graph.

**Wiring** orchestrates the Open Inventor integration: action lifecycle,
traversal callbacks, state collection, invocation order, fallback, target
selection and result publication.

For every change:

1. State which responsibilities are affected and which remain unchanged.
2. Preserve the existing public experimental API unless a separate ABI change
   is explicitly approved.
3. Give each semantic decision one owner. Repeat boundary validation only for
   memory, transport and ABI safety.
4. Prefer Coin types and conventions over a parallel representation made only
   to satisfy the planning model.
5. Use a conservative rebuild or fallback when reuse cannot be proven.
6. Keep unrelated changes in `src/base`, `src/threads` and
   `include/Inventor/threads` outside WebGPU commits.
7. Validate Recording and Rust builds. Product claims require Release median,
   p95, visual comparison, memory and phase-separated measurements. Do not
   assume direct RTT or asynchronous readback improves elapsed time.

## Prompt 001: atomize and reuse FFI packing

**Intent:** Separate the C++ `FramePlan` to private C ABI conversion from GPU
submission. Make its ownership explicit and reuse the packed immutable payload
when the same non-zero frame revision is submitted again.

**Core:** unchanged. `FramePlan` and its Coin-native calculations retain their
current meaning.

**Shell:** extend phase diagnostics with a packing-cache hit indicator. Do not
change public error strings except where required to preserve an existing
failure.

**Infra:** introduce a private owner for all arrays referenced by
`CoinWgpuFrameView`. It copies an immutable revision once, updates target
dimensions without repacking, and never exposes the private FFI in public Coin
headers. A zero revision is never reusable. Texture bytes must be owned by the
packed frame so no pointer outlives its source `FramePlan`.

**Wiring:** unchanged. `SoWgpuRenderAction`, traversal callbacks, render target
selection and frame execution order must behave exactly as before.

**Positive oracle:** two submissions of the same non-zero revision produce the
same image and the second reports a packing-cache hit.

**Negative oracle:** a new revision is repacked even if its vector sizes match.

**Unknown oracle:** revision zero is always repacked.

**Gates:** Rust and Recording suites pass; public `libCoin` symbols remain
unchanged; static Release packing time improves without visual or memory
regression.

### Prompt 001 result — 2026-09-24

- Debug/Rust: 38/38 complete tests passed; the 29 WebGPU tests include the new
  positive, negative and zero-revision packing oracles.
- Debug/Recording: 24/24 complete tests passed; the Rust-only packer is not
  part of that target.
- Release/RADV RENOIR, Assembly, 512x512, two warmup and five traced frames:
  cached packing median was 0.000531 ms (`pack_cache_hit=1`), versus the
  previously documented 0.162 ms for the same static phase. This is a 99.7%
  phase reduction; it is not presented as a 99.7% whole-frame improvement.
- Release/RADV RENOIR, Assembly, eight warmup and 30 measured frames: WebGPU
  median/p95 was 1.959/2.327 ms including RGBA readback. The separately run GL
  median/p95 was 0.453/0.851 ms; this GL result differs materially from the
  earlier campaign and is retained as a gate, not combined into a speedup
  claim.
- The isolated WebGPU process reached 140588 KiB peak RSS. The prior campaign
  recorded 148424 KiB, so no memory regression was observed, but the runs are
  not treated as a controlled memory reduction measurement.
- Assembly `BASE_COLOR` retained exact parity after the expected vertical
  flip: RGB MAE 0 and silhouette IoU 1.

## Planned prompts

### Prompt 002: FramePlan algorithms

**Intent:** Remove deterministic frame and image algorithms from action
orchestration. Keep the action responsible for Open Inventor traversal and
ordering, not for interpreting every captured field byte by byte.

**Core:** make FramePlan validation and conservative payload equality explicit
operations on the Coin-native plan. Add an image transformation that converts
tightly packed top-origin RGBA8 readback to Coin's texture origin. The
transformation validates dimensions before mutating data.

**Shell:** keep all existing validation diagnostics byte-for-byte stable. A
new image-boundary diagnostic may exist only for the previously unreachable
case in which a supposedly complete RGBA8 result has invalid dimensions.

**Infra:** unchanged. Device, submission, GPU resources and readback transport
retain their owners.

**Wiring:** SoWgpuRenderAction invokes the Core operations. RTT deduplication
continues to ignore only FramePlan revision; staged SoSceneTexture2 continues
to flip readback exactly once before publishing it to traversal state.

**Positive oracle:** equal plans with different frame revisions compare equal,
and a 2x2 RGBA8 image has its two rows exchanged.

**Negative oracle:** changing any backend-visible payload rejects plan reuse,
and an inconsistent image byte count is rejected without mutation.

**Unknown oracle:** padding in plain captured snapshots may conservatively
prevent reuse but must never allow different captured bytes to compare equal.

**Gates:** existing validation tests retain their exact messages; the new Core
test passes in Recording and Rust builds; all product suites remain green.

### Prompt 002 result — 2026-09-24

- FramePlan validation and conservative payload equality moved out of the
  action and into compiled Core code. The validation messages and public
  experimental API signatures did not change.
- Staged SoSceneTexture2 row conversion moved to SoWgpuImageCore, which rejects
  inconsistent dimensions without modifying the caller's buffer.
- Debug/Rust passed 39/39 tests; Debug/Recording passed 25/25; Release/Rust
  passed 39/39. WgpuFrameCoreTest covers the positive and negative oracles.
- Doxygen 1.9.8 generated the experimental overview and the
  SoWgpuRenderAction, SoWgpuRenderTarget and SoWgpuSceneManager pages. The
  build retains Coin's pre-existing undocumented-member warnings.
- Dynamic-symbol inspection found the new Core entry points only in
  libCoinWgpuExperimental, with no FramePlan or SoWgpuImageCore symbol added
  to libCoin.
- This atomization does not claim a frame-time improvement; its purpose is to
  give later performance changes a single testable owner.

### Prompt 003: diagnostic Shell

**Intent:** Give language-facing WebGPU outcomes one private owner without
changing the public action, target or scene-manager interfaces.

**Core:** unchanged. Validation and transformation continue to return
mechanical outcomes and diagnostics without printing.

**Shell:** introduce a structured action diagnostic containing public status,
private domain and existing message. Own the backend/target-to-action status
tables, stable status/domain names, trace enablement and deterministic C++
phase-line formatting.

**Infra:** retain translation of raw private FFI status codes into
BackendStatus. Report SubmitResult; do not choose public action policy.

**Wiring:** publish Shell results through the existing getLastStatus() and
getLastError() accessors. It still decides abort, fallback and traversal order.

**Positive oracle:** every BackendStatus has one expected public action status,
and known phase samples produce the documented key order.

**Negative oracle:** unknown/error statuses fail closed as BACKEND_ERROR;
diagnostic context must not discard the original message.

**Unknown oracle:** tracing remains opt-in by environment presence; disabling
it must avoid phase formatting on the frame path.

**Gates:** preserve public error strings and trace keys; pass Recording, Debug
Rust and Release Rust suites; keep new symbols out of libCoin.

**Result (2026-09-24):** the private SoWgpuDiagnosticShell now owns action
diagnostics, backend/target status policy and the C++ action/bridge phase
records. Debug Rust passed 40/40 tests, Debug Recording passed 26/26 and
Release Rust passed 40/40. The opt-in Release trace retained the existing
<tt>rust</tt>, <tt>bridge</tt> and <tt>action</tt> records; the new symbols
exist only in libCoinWgpuExperimental. A full Doxygen generation included the
updated experimental page. This increment makes no frame-time improvement
claim: formatting stays outside the frame path unless tracing is enabled.

### Prompt 004: traversal capture versus geometry transformation

**Intent:** Keep Open Inventor traversal authoritative while making indexed
geometry validation, triangulation, deduplication and generated normals
independently testable.

**Core:** accept Coin-native spans plus already-captured facts. Validate
indices and finite coordinates, retain the conservative Coin tessellation
fallback, generate normals with Coin facilities, resolve bindings, emit an
atomic indexed result and calculate its backend-visible payload digest. It
must not own an action, state, element, node or GPU resource.

**Shell:** unchanged. Preserve every existing indexed-geometry diagnostic.

**Infra:** unchanged. The backend continues to consume the same FramePlan.

**Wiring:** continue to own `SoCallbackAction`, `SoState`, element queries,
material lookup, render-state capture, fallback choice and atomic FramePlan
commit.

**Positive oracle:** convex faces, polylines and missing per-vertex normals
transform without a scene-graph traversal.

**Negative oracle:** invalid input returns no partial geometry and keeps the
existing diagnostic.

**Unknown oracle:** concave, twisted or general polygon topology continues
through Coin's callback/tessellation fallback.

**Gates:** direct Core tests pass in Recording and Rust builds; the existing
fast-path, visual and GL-reference suites remain green; new symbols stay out
of libCoin.

**Result (2026-09-24):** indexed face/line validation, sentinel parsing,
convex-quad triangulation, attribute binding, vertex deduplication, generated
normals and payload hashing moved to SoWgpuIndexedGeometryCore. The Builder
captures traversal facts, resolves Coin materials and commits the transformed
result atomically. Its implementation decreased from 1501 to 1021 lines while
the algorithms became callable without SoAction or SoState. Debug Rust passed
41/41 tests, Debug Recording passed 27/27 and Release Rust passed 41/41,
including the existing fast-path and GL-reference tests. Doxygen regenerated
the experimental overview without a warning for the new Core, and dynamic
symbols remain confined to libCoinWgpuExperimental. This atomization makes no
frame-time improvement claim.

### Prompt 005: typed frame reuse

**Intent:** Replace the binary whole-plan cache decision with an explicit,
testable relationship between consecutive private FramePlans. Preserve the
existing static-scene traversal skip while making smaller future updates
possible without guessing about external resource ownership.

**Core:** classify two nonzero revisions as exact reuse, camera-only patch,
resource rebuild, full rebuild or unknown. A camera patch requires equal
camera-independent payload, stable camera slots, and render-state view and
projection matrices that agree with their referenced camera snapshots. Direct
GPU texture tokens and unversioned plans are unknown.

**Shell:** give every result one stable lowercase name. Extend the existing
opt-in phase lines with `plan_reuse` and `pack_mode` while retaining
`plan_cache_hit` and `pack_cache_hit` for existing consumers.

**Infra:** accept a camera patch only when its `baseRevision` equals the
revision currently owned by the FFI packer. Repack render states and their
derived model-view, MVP and normal matrices; preserve packed geometry,
indices, draws, materials, textures and samplers. A stale or missing base
revision performs a full pack.

**Wiring:** skip traversal for an unchanged root as before. After a changed
root is captured and validated, classify it against the prior plan from the
same root, reuse the prior immutable revision when equal, and pass the typed
decision through the target to Infra. RTT direct mode remains unknown and
keeps its conservative path.

**Positive oracle:** equal payload reuses the previous revision; a base-color
camera move is classified and packed as `camera_patch` without repacking
geometry.

**Negative oracle:** a stale camera-patch base revision forces a full pack;
geometry changes never classify as a camera patch.

**Unknown oracle:** unversioned plans and plans containing opaque connector
texture tokens are unknown and receive no partial-reuse promise.

**Gates:** direct Core and FFI tests cover all outcomes and stale-base
fallback; camera, lighting and visual suites stay green; Debug Recording,
Debug Rust and Release Rust pass; new symbols remain outside `libCoin`.

**Result (2026-09-24):** SoWgpuFrameReuseCore now classifies all five
outcomes, Wiring records the selected outcome, and the Rust Infra accepts a
camera patch only when its owned packed revision equals the classified base.
The Shell retained the old cache-hit keys and added `plan_reuse` and
`pack_mode`. Debug Rust passed 42/42 tests, Debug Recording 28/28 and Release
Rust 42/42; Doxygen generated the typed-reuse section, and dynamic symbol
inspection found the new entries only in `libCoinWgpuExperimental`.

The non-gating Release `WgpuFrameReuseBenchmark` compares both pack paths in
one process with the Assembly payload sizes (22,005 vertices and 131,169
indices). Across 250 samples, full packing measured median/p95
0.051676/0.055704 ms and camera patch 0.000240/0.000250 ms, a 99.54% median
reduction for this isolated phase. A real five-frame Assembly camera trace
selected `camera_patch` on every measured frame and reported pack median/p95
0.003917/0.004568 ms.

This is not a whole-frame speedup claim. In the separate same-process
Release comparison (512x512, eight warmup and 30 measured frames, RGBA
readback included), dynamic Assembly measured WebGPU median/p95
22.3452/24.1579 ms and GL 0.439217/0.792696 ms. The aligned first-frame visual
gate remained exact (RGB MAE 0, silhouette IoU 1). An isolated WebGPU run
reported 144496 KiB peak RSS. Traversal plus FramePlan construction still
dominates the dynamic workload, and the exported meshes still do not measure
the real FreeCAD viewport.

### Prompt 006: camera-only overlay with Coin notification proof

**Intent:** Avoid the expensive geometry traversal when a supported static
scene changes only its camera, without bypassing Open Inventor semantics or
changing Coin 4 public ABI. Preserve full traversal as the default.

**Core:** given an existing validated FramePlan and a newly captured
CameraSnapshot, copy immutable payload and replace only camera-dependent
states. Refuse PHONG, fog, multiple cameras, opaque connector tokens and
inconsistent render-state camera references. Use one revision source for full
plans and overlays.

**Shell:** preserve existing diagnostics and phase keys; expose the same
`camera_patch` choice for a traversal-free overlay as for a classified full
capture. Do not equate a trace with an untraced performance sample.

**Infra:** keep ownership of packed buffers and the stale-base fallback. No
new direct RTT or asynchronous timing claim.

**Wiring:** attach an immediate SoNodeSensor to the last successful root.
Permit the overlay only for an exact SoSeparator with a direct first-child
Perspective/Orthographic camera, uncropped viewport and a closed exact-type
allowlist of camera-independent groups, geometry and state nodes. Every
notification must originate in a camera field; structural, geometry, mixed
or unknown notifications force a normal Coin traversal. Capture the camera
through Coin's view-volume API, then ask Core for the mechanical overlay.

**Positive oracle:** moved-camera Recording output and GPU pixels equal a
fresh full traversal, while phase tracing shows `plan_cache_hit=1`,
`plan_reuse=camera_patch` and microsecond traversal.

**Negative oracle:** geometry edit and child insertion match a fresh full
traversal. PHONG and fog refuse the Core overlay.

**Unknown oracle:** custom nodes, camera-dependent traversal, paths,
multiple cameras, direct RTT and unsupported viewport mapping retain the
full path.

**Gates:** Debug Rust 42/42, Release Rust 42/42 and Debug Recording 28/28
tests (including 33/33 and 19/19 WebGPU-specific tests respectively); exported
Assembly BASE_COLOR GL comparison on the same AMD GPU remains aligned RGB
MAE 0 and silhouette IoU 1. Exported meshes are not the FreeCAD viewport.

**Result (2026-09-24):** the Assembly dynamic benchmark at 512x512, eight
warmup and 30 measured frames, including RGBA readback, changed from the
previous WebGPU run's median/p95 22.3452/24.1579 ms to 4.62694/6.20546 ms
in a same-process AMD RADV RENOIR comparison. Coin GL measured
0.46698/0.935525 ms in the new run: this is a WebGPU improvement, not GL
superiority. A separate WebGPU process measured 4.55145/5.36009 ms and
141972 KiB peak RSS; a separate GL process measured 0.398563/0.679306 ms
and 96440 KiB. Different processes are not a memory-reduction claim.
The remaining costs include whole-plan copying and Rust bridge validation.

### Prompt 007: transactional camera overlay and target validation

**Intent:** Remove whole-FramePlan copying and repeated C++ profile validation
from proven camera-only frames without weakening the independent Rust FFI
boundary. Keep the Open Inventor notification and exact-type proof from
Prompt 006.

**Core:** patch only the camera snapshot, dependent render-state matrices,
derived fog distance and revision of an already validated private FramePlan.
Validate the new camera before mutation and retain a small undo record; do not
copy geometry, textures or draw packets. A failed apply must restore the
original plan and revision exactly.

**Shell:** retain existing status strings and phase keys so before/after
profiling remains comparable. Never interpret a traced frame as an untraced
latency sample.

**Infra:** the C++ target may reuse profile validation only if the camera
patch's base equals the revision last validated for that target. Resize or a
stale base must use full validation. The Rust bridge still validates its
incoming FFI view on each new revision; optimizing that boundary needs a
separate proof.

**Wiring:** apply the Core overlay to the action's cached plan only in the
already-proven camera-only path. Commit on success; roll back on any action or
target failure. Plan-only traversal is unchanged.

**Positive oracle:** geometry vector addresses remain stable, Recording and
GPU pixels match a fresh traversal, and the traced `frame_plan_ms` falls to
microseconds for the exported Assembly camera path.

**Negative oracle:** invalid camera fails before mutation; a failed
`applyAsync()` with no target restores the prior plan and can retry; stale
target base and resize force full validation.

**Unknown oracle:** unsupported nodes, lighting, fog, viewport changes and
opaque resources keep the prior conservative full path.

**Gates:** full Debug Rust, Release Rust and Debug Recording suites, Release
median/p95 against Coin/GL on the same AMD GPU, first-frame visual gate,
memory observation and no new `libCoin` symbols. A smaller phase time is not
automatically a whole-frame win.

**Result (2026-09-24):** the transactional plan and target validation gate
passed Debug Rust 42/42, Release Rust 42/42 and Debug Recording 28/28.
Assembly camera movement preserved exact GPU pixels against full traversal;
the first-frame Coin/GL gate remained aligned RGB MAE 0, silhouette IoU 1.
With eight warmup and 30 measured frames on AMD RADV RENOIR at 512x512,
including RGBA readback, the final paired run gave WebGPU median/p95
3.15660/4.32228 ms and Coin/GL 0.599847/1.01720 ms. A separate WebGPU
process reached 140632 KiB peak RSS versus 96552 KiB for a separate GL
process. Trace-only camera frames showed ~0.015–0.025 ms FramePlan work but
~0.57–0.79 ms Rust validation; variable GPU wait and readback remain major
costs. Across-version runs are not paired and GL times varied, so the phase
reduction is established more strongly than a whole-frame speedup. The
exported Assembly is not FreeCAD's actual viewport.

### Prompt 008: owned Rust context

Replace ambient connector state with explicit private context ownership without
changing public Coin ABI. Device, caches, serials, readback and fault injection
must have a defined owner and loss/recovery lifecycle.
