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

#### Prompt 008A: device-owned validated camera scene

**Intent:** Remove repeated geometry/composition validation from proven
camera-only frames while keeping Rust responsible for memory and ABI safety.
This is one slice of Prompt 008, not a completed removal of all ambient state.

**Core:** unchanged; Coin-native camera overlay and Open Inventor traversal
proof remain those of Prompts 006–007.

**Shell:** preserve error status and phase keys. Report phase reductions and
whole-frame latency separately.

**Infra:** bump only the private C++/Rust protocol to 17. The C++ packer
emits a nonzero camera base only when it actually reused its immutable arrays.
On a fully validated, opaque, untextured frame, the Rust device owns a copy of
vertices, indices, draws, materials, states and composition order, capped at
32 MiB of retained CPU data. Its camera
patch uses those owned arrays and validates the changed state; it does not
dereference caller geometry pointers. The snapshot belongs to one device
generation and disappears on device loss/destruction. Any missing/stale base,
size, count, state or generation mismatch takes the full FFI path. Textures,
RTT and transparent composition stay on that path.

**Wiring:** unchanged; action and target continue to use their existing
conservative camera decision and rollback.

**Positive oracle:** a patch with null caller geometry pointers renders the
same pixels as a full reference on another device; Rust `validation_ms` drops
without new geometry uploads.

**Negative oracle:** stale base, a changed non-camera state, RTT and a new device
with the old base reject null geometry pointers without publishing pixels or
an RTT token.
Full C++ packing clears the camera hint.

**Gates:** full Rust/Recording suites, Release paired median/p95 versus GL,
visual MAE/IoU and isolated peak RSS. No new `libCoin` symbols. The actual
FreeCAD viewport remains outside scope.

**Result (2026-09-24):** Debug Rust 42/42, Release Rust 42/42 and Debug
Recording 28/28 passed. On Assembly camera frames the traced Rust validation
fell from ~0.57–0.79 to ~0.004–0.009 ms. Three untraced Release/AMD paired
30-frame runs gave WebGPU medians 2.058–2.163 ms versus GL 0.404–0.477 ms;
their p95s were 2.551–2.840 and 0.789–0.903 ms respectively. The earlier
WebGPU run was 3.157/4.322 ms, but not interleaved with these runs, so the
phase reduction is the firm claim. Isolated WebGPU peak RSS was 142008 KiB,
versus 140632 KiB in an earlier process; the owned copy has a 32 MiB cap.
The first-frame GL gate remained MAE RGB 0/IoU 1. The FFI test proves that
camera patches can render from Rust-owned geometry with null caller geometry
pointers, while stale/changed-state/device cases fail closed.

#### Prompt 008B: explicit offscreen readback outputs

**Intent:** Remove depth readback when a consumer needs only RGBA, without
changing Coin 4's public ABI, depth testing or the default output contract.
Keep the GL comparison honest about which attachments it times.

**Core:** unchanged. Render geometry and depth testing are independent of
which CPU outputs are requested.

**Shell:** the benchmark defaults to `--readback color` for a comparable GL
run. `--backend wgpu --readback color-depth` measures the legacy output
contract separately. Do not label the latter as equivalent to GL color-only.

**Infra:** the existing private target POD already represents an omitted depth
output with a null pointer and zero length, so the Rust protocol stays at 17.
The connector skips depth staging/copy/map in that case; async tickets record
`depthFormat=0`, `depthBytes=0`. RTT GPU-only remains unchanged.

**Wiring:** `SoWgpuRenderTarget` keeps depth readback enabled by default and
offers an explicit experimental offscreen switch. It invalidates synchronous
accessors on change; a submitted async ticket retains its own output policy.
Window and direct RTT targets reject the switch. Recording/CPU still uses
internal depth for rasterization while hiding unrequested depth output.

**Positive oracle:** offscreen color is bit-identical with/without depth
readback on the same scene; restoring depth returns a populated output;
color-only tickets survive a later policy change. The exported-scene visual
gate matches GL after origin alignment.

**Gates:** Release/Debug Rust and Recording suites, median/p95 on the same
AMD GPU, visual MAE/IoU, isolated peak RSS, Doxygen and no new `libCoin`
symbols. Historical RGBA-labelled WebGPU runs copied depth internally and
are not a directly paired baseline for the new benchmark default.

**Result (2026-09-24):** the gates passed 42/42 Release Rust, 42/42 Debug
Rust and 28/28 Recording. Release/AMD RADV RENOIR, `BASE_COLOR`, 512x512,
camera changed each frame, 10 warmup and 60 measured frames: PartDesign
RGBA-only WebGPU median/p95 1.281/1.621 ms versus Coin/GL 0.364/0.568;
separate WebGPU color+depth 3.083/3.567 ms. Assembly RGBA-only WebGPU
1.725/2.202 versus GL 0.429/0.882; separate color+depth 2.165/2.574.
At 1024x1024 PartDesign, a 40-frame RGBA-only paired run measured WebGPU
5.187/6.119 versus GL 1.141/1.822 ms; isolated WebGPU color+depth was
11.942/13.023 ms. Isolated peak RSS was 146744 KiB for color-only and
154864 KiB for color+depth at 1024x1024, one process each. The visual gate
reported aligned MAE RGB 0/IoU 1 on Assembly and 0.001358/0.999978 on
PartDesign. These are exported meshes, not the FreeCAD viewport. Color-only
is a measured improvement, not WebGPU/GL performance parity.

#### Prompt 008C: bounded device-owned readback staging

**Intent:** Reuse completed offscreen staging buffers and remove the extra
CPU color copy when only RGBA is requested. Do not substitute frame N-1 for
frame N or describe asynchronous submission as lower end-to-end latency.

**Core:** unchanged; the row-pitch transformation remains mechanical and
retains the color+depth transactional publication rule.

**Shell:** extend opt-in Rust phase tracing with `staging_color_reused=0|1`.
Report phase timings separately from untraced whole-frame medians/p95s.

**Infra:** a pool belongs to each Rust device generation. Acquire exact-size
unmapped MAP_READ|COPY_DST buffers; recycle only after a successful map,
completed GPU work, dropped mapped view and `unmap()`. Keep at most two free
buffers per size, 16 free buffers and 16 MiB free per device. Buffers above
8 MiB are not retained. In-flight async tickets keep their own pool owner.
Cancellation retires a buffer only after its map completes. A new device
generation gets a new pool; old-generation tickets are invalidated rather
than recycled into it. Color-only sync and async outputs copy mapped rows
directly to their caller buffers after validating the full mapped extent;
color+depth continues to stage both CPU attachments until both
maps and copies succeed.

**Wiring:** unchanged. Existing synchronous and asynchronous contracts,
including ticket generation/serial and target readback policy, are preserved.

**Positive oracle:** repeated alternating-color sync and async frames never
publish a previous frame; trace shows one initial staging allocation followed
by reuse. Color-only and color+depth outputs remain correct.

**Negative oracle:** pending tickets remain independent across resize and
device loss; failed maps, cancelled tickets and invalid output buffers do not
publish partial attachments or return a buffer to the pool prematurely.

**Gates:** full Release/Debug Rust and Recording suites, visual MAE/IoU versus
GL, Release median/p95 and isolated peak RSS. Do not claim an A/B speedup from
non-interleaved historical runs. No new public Coin 4 ABI or private FFI
protocol fields.

**Result (2026-09-24):** Release Rust 42/42, Debug Rust 42/42, Recording
28/28. The first traced RGBA frame had `staging_color_reused=0`; later
frames had 1. See the reproducible campaign in
`wgpu-freecad-examples-validation.md`. At 512² PartDesign WebGPU beat GL
in two of three paired medians, while Assembly remained behind in all three;
there is no general median advantage. At 1024² PartDesign remained slower
than GL. The next candidate is persistent camera-frame GPU uniforms/bind
groups, measured independently.

#### Prompt 008D: bindings persistentes para patches de câmera

**Intent:** testar se manter o storage de materiais, uniform buffers e bind
groups no dispositivo reduz a preparação de frames com apenas câmera alterada.
Não presumir ganho end-to-end: comparar o mesmo binário com a opção ligada e
desligada, incluindo GL, readback, p95, imagem e RSS.

**Core:** inalterado. A decisão de patch continua a exigir estados
independentes da câmera idênticos e geometria validada pertencente ao Rust.

**Shell:** a telemetria opt-in acrescenta `camera_bindings_created` e
`camera_bindings_reused` ao trace Rust. O ensaio não promove ganho de CPU
isolado a ganho de frame completo.

**Infra:** `COIN_WGPU_CAMERA_BINDINGS=1` habilita o experimento por processo;
o padrão é desligado. Um cache privado por dispositivo identifica a mesma
`Arc<ValidatedGeometry>`, retém o buffer imutável de materiais e até um
uniform buffer/bind group por draw. `Queue::write_buffer` atualiza apenas
uniforms já existentes antes do submit seguinte. O cache limita-se a 512
draws e 4 MiB de payload calculado de materiais+uniforms, sendo descartado
ao trocar geometria, ao renderizar sem patch elegível ou ao recriar o device.
Texturas, RTT e janela continuam no caminho anterior. Nenhum campo FFI ou
símbolo público do Coin 4 foi acrescentado.

**Wiring:** a action e o target conservam a escolha de patch anterior.
Somente o encoder Rust recebe a geometria validada quando essa escolha já
foi confirmada pelo dispositivo e pela revisão-base.

**Positive oracle:** três revisões sucessivas com ponteiros de geometria
nulos no patch igualam pixel a pixel frames completos num segundo device;
o primeiro patch cria e os seguintes reutilizam o binding. Dois patches
assíncronos submetidos sem aguardar o primeiro readback conservam cada um
sua própria câmera. A imagem exportada em `BASE_COLOR` mantém MAE/IoU
contra GL.

**Negative oracle:** revisão-base obsoleta, estado não relacionado à câmera,
RTT e device recriado não leem ponteiros nulos nem publicam pixels; frames
fora do perfil invalidam os bindings. A memória retida é limitada.

**Gates/result (2026-09-24):** Release Rust 42/42, Debug Rust 42/42 com a
opção ligada e Recording 28/28; comparação visual PartDesign
MAE 0,001358/IoU 0,999978 e Assembly MAE 0/IoU 1 após inverter o GL.
Seis pares A/B contrabalançados por cena a 512² reduziram a mediana WebGPU
em 18,2% no PartDesign e 10,1% no Assembly; três pares de PartDesign a
1024² não mostraram ganho robusto do p95 nem vantagem geral sobre GL.
O experimento permanece opt-in. Medidas, RSS e reprodução em
`wgpu-freecad-examples-validation.md`. O comparador
visual de cena completa não testa a viewport real do FreeCAD.

#### Prompt 008E: separar preparação, execução e readback

**Intent:** localizar o gargalo antes de alterar o pipeline. Medir
attachments, encode, staging, submit, espera, map, cópia/publicação e
reciclagem separadamente; distinguir trabalho GPU de espera CPU. Preservar
o caminho sem trace e a ABI pública Coin 4.

**Core:** sem alteração de transformação ou contrato de pixels.

**Shell:** `COIN_WGPU_TRACE_PHASES=1` acrescenta `rust_cpu_detail` com
spans contíguos cujo total reconcilia por frame. `rust_gpu` explicita
`ok`, `disabled`, `unsupported` ou erro da sonda. Não somar medianas de
fases distintas nem chamar `device.poll` de duração da execução GPU.

**Infra:** `COIN_WGPU_GPU_TIMESTAMPS=1`, junto ao trace, solicita
`TIMESTAMP_QUERY` e `TIMESTAMP_QUERY_INSIDE_ENCODERS` somente se o adapter
oferecer ambos. Quatro queries delimitam os passes de render e as cópias
GPU→staging no offscreen síncrono. Resolve e map de 32 bytes ocorrem no
próprio frame; falha da sonda não deve alterar os pixels nem o status do
render. Sem as duas flags, nenhuma query/buffer da sonda é criada.

**Wiring:** a action, o FramePlan e a ponte C++ continuam inalterados.
O trace Rust acrescenta subfases aos marcadores já expostos pela ponte.

**Gates:** build Release/Debug Rust e Recording, testes de device loss,
trace curto com queries na AMD, 40 quadros após 10 warmup nas cenas
PartDesign/Assembly, comparação não instrumentada com GL, MAE/IoU e RSS.
Queries GPU são uma sonda intrusiva; seus tempos não formam um A/B de
latência contra a execução normal.

**Resultado (2026-09-24):** Release Rust 42/42, Debug Rust 42/42 e
Recording 28/28. As 40 amostras por cena retornaram `rust_gpu status=ok`.
Em PartDesign 1024², medianas traceadas de 1,360 ms na criação dos
attachments e 1,214 ms na cópia CPU do readback, contra 0,065 ms de
render e 0,152 ms de cópia GPU segundo queries. Em PartDesign 512²,
attachments 0,015 ms, encode 0,044 ms, cópia CPU 0,066 ms. O próximo
candidato é reutilizar attachments por target/tamanho/geração de device,
com limites de memória e invalidação rigorosa. Medidas e reprodução em
`wgpu-freecad-examples-validation.md`; não há ganho de velocidade
reivindicado por este prompt de diagnóstico.

#### Prompt 008F: attachments concluídos, RGBA contíguo e view emprestada

**Intent:** reduzir os custos encontrados no 008E sem mudar a ABI pública
do Coin 4, a identidade do frame ou a publicação atômica de cor+depth.
Medir os três ganhos separadamente e testar dois readbacks em voo antes
de chamar qualquer etapa assíncrona de aceleração.

**Core:** copiar RGBA do staging em um bloco quando o pitch não tem padding;
manter o loop por linha quando há padding. Testes com larguras 64/65 verificam
ambos. Uma flag de diagnóstico força o loop antigo para A/B.

**Shell:** documentar a validade da view emprestada e expor no trace
`attachments_reused`. O benchmark distingue saída `copy` de `borrow`,
latência síncrona de throughput/latência assíncronos e marca resultados
incomparáveis.

**Infra:** `COIN_WGPU_ATTACHMENT_CACHE=1` guarda no máximo um par
RGBA8/depth por device, tamanho e geração, com limite de 32 MiB e somente
após concluir o readback síncrono. Async e RTT não compartilham esses
attachments; resize/tamanho diferente e device loss impedem hit indevido.
O experimento de profundidade 2 usa os tickets já existentes sem alterar
`apply()`.

**Wiring:** `borrowRGBA()` entrega uma view do buffer do target após
render síncrono bem-sucedido. O ponteiro expira no próximo render, resize,
troca de política ou destruição. `readbackRGBA()` continua copiando.

**Gates:** A/B Release pareado e contrabalançado com GL na mesma GPU,
40 frames após 10 warmup em 512²/1024²; mediana, p95, qualidade, RSS;
testes Rust Release/Debug e Recording, largura com padding e device loss.
Async deve reportar throughput e latência separados, sem promessa de ganho.
Resultados, comandos e limitações em `wgpu-freecad-examples-validation.md`.

**Resultado (2026-09-24):** Release Rust 42/42, Debug Rust 42/42,
Recording 28/28 e testes Rust de cópia 2/2. Em PartDesign 1024²,
cache+cópia contígua+saída copy reduziu a mediana de 4,332 para
1,556 ms/frame (−64,1%); `borrow` chegou a 1,075 ms contra
1,421 ms do GL pareado. A qualidade 512² manteve PartDesign
MAE 0,001358/IoU 0,999978 e Assembly MAE 0/IoU 1. O pico RSS
de processo não mediu VRAM. Profundidade async 2 foi mais lenta
em throughput; ficou apenas no benchmark. Cache ainda opt-in.
