# Experimental retained rendering integration

This branch connects FreeCAD's retained capture and planner to the existing
CoinRender executor selected at build time (RECORDING, RUST_BRIDGE or BGFX).
It is an initial bounded integration, not a replacement for all CoinRender
capture profiles or a merge of the complete FreeCAD renderer stack.

## Reading this document as a FreeCAD/coin maintainer

This document describes a downstream experiment and candidate contributions.
The upstream reference is the retained stack at
[`17a9e4b21b643cb44d42e103183d8384eef6a6a5`](https://github.com/FreeCAD/coin/tree/17a9e4b21b643cb44d42e103183d8384eef6a6a5),
associated with [PR #80](https://github.com/FreeCAD/coin/pull/80).
All comparisons below use that pinned revision; they do not describe the current
default branch or establish that these changes ship in FreeCAD.

The fork already supplies scene-to-IR capture, retained updates and planning.
Its stack also contains manager orchestration, GL execution, interaction and
prepared instance submission. This integration imports only the subset recorded
in the [origin receipt](../cmake/patches/freecad-retained-origin.txt). Existing
upstream capabilities are credited to the fork, rather than proposed as new work.

For review, start with:

1. [Contribution boundaries](#contribution-boundaries-native-fork-and-coinrender):
   F1–F13 identify candidate semantic changes; C1–C20 identify integration and port work.
2. [Native contribution review](#native-contribution-review): contracts, proposed
   regression cases and evidence required before extracting a patch.
3. [Current coverage](#current-coverage): what our transport currently consumes.
   A rejected local feature may already exist in the upstream IR or GL executor.
4. [Android comparison](coin-render-retained-coupling-android.md): measured
   downstream results, environment, raw receipts and unresolved validation limits.

No upstream patch series has been extracted or validated on the fork by this
work. Feature IDs are review units, not submitted commits or acceptance claims.

## Data flow and entry point

```
Coin scene
  -> CoinRenderSceneCapture / SoIRRenderAction traversal (once)
  -> SoDrawList
  -> SoRenderPlanner::build (once)
  -> SoRenderPlan
  -> CoinRenderRetainedTransport
  -> selected CoinRender executor / CoinRenderTarget
```

`CoinRenderRetainedRenderer` is the new opt-in entry point:

```cpp
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>

CoinRenderRetainedRenderer renderer(SbViewportRegion(640, 480));
// Root may use BASE_COLOR or PHONG; sorted-object BLEND is the default.
renderer.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
CoinRenderAction::Status result = renderer.render(root, *target);
```

Existing CoinRenderAction/manager callers continue to use their existing capture.
The connector does not invoke CoinRenderAction or GLRender. The native updater
uses targeted SoCallbackAction path replay to resolve effective material and
geometry state; this is incremental state resolution, not another full capture
or planner.
`CoinRenderRetainedTransport` also implements the upstream internal SoRenderBackend
interface, so a caller already holding a draw list and plan can execute them
without traversing or planning again. Such callers must provide supported,
faithfully captured input; omitted scene semantics cannot be recovered from IR.

The adapter translates operations in supplied order. It creates a frame-local
resolved composition receipt for Target and the executor packers. A lexical
thread-local loan supplies that same schedule to resource-admission helpers
whose interfaces have no receipt parameter; it matches only the owned frame
address/revision and restores the previous scope on every exit. The receipt
bypasses composition classification/sorting, even when composition borrowing is
disabled. Ordinary frame validation, target admission, packing, submission and
transactional publication remain in place. The reversed-plan test is intentional:
two coplanar differently colored cubes must change the winning color when the
supplied draw order is reversed.

## Contribution boundaries: native fork and CoinRender

The local work has two delivery lanes. Native capture/IR/updater/planner changes
may become contributions to FreeCAD/coin. The facade, transport, frame ownership
and GPU execution remain CoinRender. A third classification records compatibility
scaffolding needed to transplant the pinned fork into this older Coin base; it
belongs to this port and is not a new feature to send back upstream.

The machine-readable [contribution map](../cmake/patches/freecad-retained-contributions.json)
classifies all 203 changed non-documentation files, including the map and its
[auditor](../scripts/coinrender/audit_retained_contribution_boundaries.py).
This is a feature/hunk boundary, not permission to copy complete files. The
reference for upstream review is FreeCAD/coin at `17a9e4b21b643cb44d42e103183d8384eef6a6a5`.

| ID | Native candidate | Local status and upstream gate |
| --- | --- | --- |
| F1 | Logical fog, clip planes, texture transform and environment/complexity/depth state | Implemented locally. Port the semantic delta and test it with the fork's own capture and GL executor. |
| F2 | World-state dependencies, qualified in-scene camera projection updates, safe hidden-tail/Switch replay | Implemented locally. Preserve the fork manager's notification authority and verify applied-root/additional-root paths. |
| F3 | Attenuation captured per light occurrence | Implemented locally. Test environment changes before/after lights and separator scope. |
| F4 | Shared cube resource, whole-object sorting center and native order policies | Implemented locally. Split geometry semantics from planner policy changes; retain generated-primitive and transparent-order equivalence. |
| F5 | Compact native commands and paths | Review required. The pinned planner/GL executor use legacy command reference/array APIs, which can expand compact storage. Adapt those consumers and measure before claiming an upstream gain. |
| F6 | Authored depth provenance | Review required. Its current consumer is principally CoinRender admission/replay. Demonstrate a native correctness case or consumer before adding this data to the fork. |
| F7 | Retained image upload quality | Review required. Separate source-image semantics from the current `getGLImageUploadHints` implementation; capture must work without a preceding executor/GL render. |
| F8 | Explicit identity matrices and transparent-black clear defaults | Implemented locally with a Coin-only default-parameter regression; no renderer dependency. Port and qualify native consumers. |
| F9 | Native PBR node, immutable material descriptors and capture/planner semantics | Capture connected from `codex/pbr-clean`; the bounded BGFX transport/shading profile is C9. Review native API and resource model before proposing upstream. |
| F10 | Optional CPU HDR environment payload, validation and preparation | Imported from the PBR branch and tested linking Coin only. Reusable module candidate, requiring a native consumer/API review; local renderer/export registration stays separate. |
| F11 | Logical PBR image roles, coordinate sets and scalar material replay | Images and coordinates remain native values; scalar edits update immutable descriptors using state paths. Mips, tangents and GPU resources stay in C16. Requires upstream consumer qualification. |
| F12 | PBR coordinate demand and direction provenance | Native/callback primitive UVs and indices remain available without images; separate direction and normal transforms; retained coordinate provenance permits lobe disable/enable without recapture. Qualify native fork consumers. |
| F13 | Optional anisotropic albedo product | Pure CPU preparation, serialization, evaluation and independent quadrature controls from pinned PBR source. Review optional SDK/API and a native consumer before upstream export. |

A local implementation or a passing CoinRender comparison does not qualify any
of these as an upstream-ready patch. In particular, preserve the fork's full
`SoLazyElementP`: the smaller file in this checkout is an older-base snapshot
compatibility seam and omits some upstream packed-alpha/blending machinery.

| ID | Kept on the CoinRender side | Main owners |
| --- | --- | --- |
| C1 | Opt-in facade, supported-profile admission and caller-driven sensor collection | `CoinRenderRetainedRenderer`, `CoinRenderSceneChanges` |
| C2 | Conversion to executor snapshots, material slots, images/samplers, stroke lowering, transport patches and target rebinding | `CoinRenderRetainedTransport` |
| C3 | Resolved native-order receipt, frame revisions, borrowing and publication | `CoinRenderComposition`, `CoinRenderFrameReuseCore`, `CoinRenderTarget` |
| C4 | BGFX lowering, instancing/upload decisions and executor controls | `src/rendering/coinbgfx/`, `testsuite/coinbgfx/` |
| C5 | wgpu envelope/FFI and executor packing | `src/rendering/coinwgpu/` |
| C6 | Cross-backend comparisons and Android/viewer benchmarks | Existing CoinRender retained tests and the Android example |
| C7 | Imported baseline, lazy-state shim, legacy accessors, dispatch and DLL adaptation | Files marked `port_compatibility`; these are not new upstream contributions |
| C8 | Local build/initialization/packaging and provenance | Split registrations by the native feature or local adapter they enable |
| C9 | PBR material transport, BGFX HDR shading/output and comparison controls | CoinRender material DTO/conversion and `CoinBgfxPbr` |
| C10 | Global IBL snapshot ownership, uploads, uniforms and controls | CoinRender target, `CoinBgfxPbrEnvironment`, target-dependent fixtures |
| C11 | FreeCAD Assembly ingress, retained renderer process and Qt viewport presentation | `freecad_retained_viewport.py`, `coin_render_freecad_viewport.cpp`, interactive macro and GUI qualification |
| C12 | Native Vulkan/BGFX presentation, Xlib host/target lifetime and presented-image qualification | BGFX PBR output, native surface contract, FreeCAD viewport connector and serial controls |
| C13 | Optional per-link material authoring, Assembly appearance presets and controlled native previews | `freecad_assembly_appearance.py`, its macro and validated `material_overrides` on the viewport connector |
| C14 | Bounded directional PCF shadows, SSAO and sRGB gradient in native PBR | Executor depth resources/shaders, target effect controls, optional application studio geometry and native comparisons; no IR/planner changes |
| C15 | Static HDR radiance reuse, depth-only effect attachments and native measurements | Native resolve/present reuse keyed by immutable borrow, options and target generation; FreeCAD memory/timing and GPU timestamp controls |
| C16 | Stored PBR maps, normal/height conversion, MikkTSpace and native viewport scalar updates | Shared surface Core, sparse transport attributes, BGFX role bindings and optional per-link map API; native image/coordinate semantics stay in F11 |
| C17 | Anisotropic BGFX direct/IBL shading and FreeCAD controls | Bounded VNDF source integration, cube-array/LUT resources, scalar strength/rotation and same-camera native previews. No local probes or extra capture/planner. |
| C18 | Closed-solid CAD tessellation validation | Application ingress checks mesh closure/orientation and retries incomplete tessellation with explicit MeshPart angles before Coin capture. No shader, fork or planner change. |
| C20 | Rough planar PBR reflections | Pinned 128-tap GGX filter and canonical finite proxy over C19 HDR; explicit rough profile, source mapping controls and same retained geometry/planning. |
| C19 | Smooth planar PBR reflections | One auxiliary HDR/depth GPU pass over retained buffers/order; reflected camera/light/tangent frames, shared world shadows/IBL, bounded resources and native previews. Main capture/planner remain authoritative. |

CoinRender is the shared integration side, not exclusively GPU-specific code:
its recording/CPU controls also consume that contract. Reused `ImageCore`,
`StrokeCore`, sampling and plan-assembly helpers remain there while they operate
on CoinRender DTOs. A portable algorithm can be proposed separately, without
making native capture depend on those DTOs or duplicating scene planning.

Files such as `SoShape.cpp`, `SoRenderIR.cpp` and the CMake/initialization files
contain several concerns. Their file-level label does not make the entire file
exportable. Existing fork implementation, local compatibility code, portable
semantic fixes and integration registration must be reviewed as separate hunks.

The contract stays:

```
native scene -> native capture / updater -> native plan
                                              |
                                  CoinRender transport
                                              |
                                 target / selected executor
```

Native candidates must not include CoinRender target/transport headers or use
wgpu/BGFX types. The transport consumes the supplied order and resolved values;
it does not recover missing scene semantics through a second traversal or plan.
Target capability checks, device resources, execution policy and publication
remain on the integration/executor side.

Run the dependency and coverage audit without a GPU:

```
python3 scripts/coinrender/audit_retained_contribution_boundaries.py
```

An optional `--upstream-git-dir` points to an existing local fork clone and adds
byte-comparison receipts against the pinned revision; it performs no fetch and
exports/applies no patch. Coverage is measured against base commit `e8c7003345`,
so it continues to include contribution files after local commits are made.
The dependency scan is a narrow architectural guard, not proof of full behavior,
ABI compatibility or upstream portability.

Suggested contribution order is F1/F3/F8, then F2 and the separately reviewed F4
changes. F5 needs native consumer work and benchmarks. F6/F7 stay under review
until their contracts and native use cases are demonstrated. Native patches must
build on the fork with CoinRender absent, preserve its richer private helpers,
and run its own IR/plan/replay/material/GL/interaction regressions. Current
CoinRender feature scenarios can supply fixtures, but their target-dependent
binaries are not substitutes for those native tests.

## Native contribution review

### Ownership and update contract

`SoIRRenderAction` owns logical scene interpretation. `SoDrawList` retains the
captured resources and occurrences, and the native updater resolves eligible
changes or requests recapture. `SoRenderPlanner` owns operation order. Executors
consume those resolved values and perform device-specific lowering and uploads.
In the fork, the existing manager remains responsible for roots, notifications,
viewport/camera state and render lifecycle. The local facade and sensor collector
are not replacements for that manager.

| Event | Required behavior | Review observation |
| --- | --- | --- |
| Unchanged scene and parameters | Reuse captured data and plan | No full traversal or planner rebuild; the executor may still submit/present. |
| Eligible translation, material or coordinate edit | Resolve through the native updater; patch affected retained state | Verify changed occurrence/resource identity, effective inherited state and unchanged neighbors. |
| Qualified single in-scene camera projection edit | Update captured camera state and transport where admitted locally | Exercise both ordinary and compact storage; qualification failure must preserve correctness through recapture. |
| Topology, unsupported dependency or ambiguous replay | Recapture and rebuild the native plan when required | No partial/stale frame publication; conservative fallback is a valid result. |
| Native order invalidation | Rebuild native order as required | The connector consumes it without a second classifier or sorter. |
| Target/device option change | Revalidate executor resources and rebuild transport where required | Do not use transport reuse as authority to skip target admission. |

“Single capture and planning” means one authority for each. It does not prohibit
necessary native recapture or replanning after invalidation. Targeted state replay
is part of incremental resolution; it must not become another full scene capture.
These are review requirements, not claims that every event can always be patched.

### Proposed native regression matrix

The following cases should be implemented in the fork's own test infrastructure
when porting each candidate. They are proposed acceptance cases; the existing
CoinRender binaries do not establish native fork coverage.

| Candidate | Minimum native cases | Required comparison |
| --- | --- | --- |
| F1: logical scene state | Fog modes, transformed clips, texture matrices, ignored fields, nested separator restore | Captured values against effective scene state; native rendering against the fork's reference behavior, including after state edits. |
| F2: dependencies and updates | Camera/light/clip before an edited transform; applied root and manager additional roots; hidden Switch state changes and failed replay; projection edits | Updated frame against forced recapture, including unchanged siblings and resource identity; assert fallback when eligibility is lost. |
| F3: per-light attenuation | Environment before/after a light, multiple light occurrences and separator scope | Earlier light occurrences retain the attenuation effective at their traversal; later environment changes do not rewrite them. |
| F4: geometry and ordering | Cube face normals/UVs/corners, alternate material bindings, multiple material batches per object and transparent overlaps | Shared geometry against generated primitives; whole-object ordering and final rendering against the reference. Review planner policy separately from cube sharing. |
| F5: compact storage | Ordinary/compact commands, exact model reconstruction, path ownership, updates, legacy array/reference access and interaction | Equal command/resource/order semantics; record storage expansion and peak/steady memory with the fork's actual planner, GL executor and manager. |
| F6: depth provenance | Authored, inherited and ignored depth fields, nested restore | First demonstrate a native consumer/correctness case; then assert masks and resolved depth behavior there. |
| F7: texture quality | Cold logical capture before any GL render, quality zero and subsequent changes, image replacement | Correct retained image semantics without prior device state; review the current GL-hints dependency before porting. |
| F8: default parameters | Default construction followed by capture/plan with no caller matrix assignment | Explicit identity view/projection matrices and deterministic results. |

All native candidates must build with CoinRender, wgpu and BGFX absent. Preserve
existing fork material, raster, stage, picking and GL contracts during the port.
Use the fork's test commands at the pinned checkout; this document does not
invent CMake options or test names for a port that has not yet been made.

### Patch preparation and evidence

Prepare one semantic change with its native regression cases per review unit.
F1/F3/F8 can be reviewed first, followed by F2 and separate F4 geometry/planner
changes. F5 needs consumer adaptation and measurements; F6/F7 need their stated
contract decisions before extraction.

Each proposed patch description should record:

- The exact fork base revision, candidate ID, triggering scene and resulting behavior.
- The semantic hunks and any registration/API changes, excluding already imported
  upstream code and older-base compatibility shims.
- Native build/test commands and results, including relevant GL and interaction
  regressions; list any untested configurations or remaining failures.
- Forced-recapture equivalence for incremental changes, and performance receipts
  when a performance claim is made.

Keep existing upstream private helpers intact, especially `SoLazyElementP`.
Review changes in mixed files against the pinned fork rather than exporting the
local file as a replacement. The machine-readable contribution map is the path
index for this work; its labels do not certify every hunk in a file.

From this downstream checkout, run the dependency/coverage guard:

```sh
python3 scripts/coinrender/audit_retained_contribution_boundaries.py
# Optional: compare bytes with an existing bare fork clone; no network access.
python3 scripts/coinrender/audit_retained_contribution_boundaries.py \
  --upstream-git-dir /path/to/freecad-coin.git \
  --output /tmp/retained-contribution-audit.json
```

A successful guard checks classification and selected dependency boundaries.
It does not establish API/ABI compatibility or replace a native fork build.

### Performance evidence and attribution

The [six-process Android campaign](coin-render-retained-coupling-android.md)
compares the current CoinRender capture with this retained integration, using
one million buildings, the same camera and the same wgpu/GLES executor in an
x86_64 emulator. Median first render fell from 9.715 s to 4.354 s; static frame
medians were close (19.829 ms and 19.688 ms). Native allocated heap fell by
13.90 MiB, about 0.37%. RGBA and depth buffers were byte-identical.

Those numbers measure the combined downstream path, including capture, storage
and transport choices. They do not isolate F1–F8, qualify BGFX on Android, measure
a physical Android device or prove a gain for the fork's own GL executor. The
F9 was added after that campaign. The
report records paging variation and an unresolved current-path texture-sampling
comparison against legacy CoinGL. Use its receipts when quoting the result.

For a native optimization claim, compare the fork before/after the proposed patch
on the same scene, camera, device and build configuration. Report initial capture,
static submission, eligible edits, forced recapture and memory separately. Include
repeat count, timing scope, correctness checks and fallback counts. For compact
storage, additionally show whether actual consumers expand it; reduced retained
bytes alone do not establish reduced process memory or faster submission.

## PBR connection from codex/pbr-clean

Source: `codex/pbr-clean` at `ce72805f0536f8f0ef07dc5575d301b3ddffa507`.
The initial [capture-only receipt](validation/coin-render-retained-pbr-20261028/results.json)
remains historical. The follow-up now connects an opaque PBR factor profile to
BGFX/Vulkan, using the original PBR shaders and material DTO. It does not merge
the older monolithic backend into the current modular executor.

```
SoPBRMaterial -> native SoPBRMaterialElement -> SoIRRenderAction
  -> SoMaterialData::pbr + SO_SHADING_PBR_METALLIC_ROUGHNESS
  -> native SoRenderPlanner -> CoinRenderRetainedTransport
  -> typed CoinRender PBR material -> BGFX RGBA16F scene
  -> Neutral tone mapping + one sRGB encoding -> normal target publication
```

The descriptor is immutable and shared by draws using the same effective state.
It stores authored factors, including HDR emission, separately from legacy fields.
Separator restoration, ignored fields, PBR/legacy transitions and overrides follow
the source node contract. Compact command keys serialize descriptor values;
material comparisons do not depend on shared-pointer addresses. The F9 native
candidate has no CoinRender DTO, shader, device handle or GPU pass dependency.

C9 owns the conversion to `CoinRenderPbrMaterialSnapshot`, BGFX uniforms, resource
lifetime, attachment choice and output pass. The value converter also feeds the
existing callback capture as an independent control. The retained renderer never
calls that capture: it consumes the native plan through its resolved-order receipt,
without another classifier, sorter or scene traversal. The HDR/output passes are
executor lowering of that supplied order.

### Connected profile

- Native Vulkan rasterization, offscreen targets, one sample and one pass in the initial C9 campaign; C12 below adds native window presentation.
- Opaque triangles with one overall PBR material per draw and PHONG light-model state; base color, metallic,
  roughness, HDR emission and clearcoat factors.
- Directional, point and spot lighting, perspective/orthographic cameras,
  transformed normals, clipping and ordinary depth state.
- RGBA16F scene radiance; the original PBR depth policy prefers D24S8 when
  supported, with D32F fallback. Output preserves that scene depth and applies
  Neutral/sRGB with PBR output exposure (default zero stops).

The initial C9 profile excluded PBR textures, normal/bump maps and effects.
C14 adds bounded directional shadows/SSAO and C16 below connects stored PBR
images and normal/height maps. C17 connects anisotropy and VNDF global IBL.
C19/C20 add smooth and finite-proxy rough planar reflections. Local probes, refinement/area
passes, mixed legacy surfaces, scene layers, fog, alpha tests, MSAA and
non-Vulkan APIs remain outside the connected PBR profile.
Native Xlib window presentation is qualified separately in C12 below.
Unsupported combinations are refused explicitly. Global isotropic IBL is connected separately below.
C16 resolves image roles/normal coordinates; C17 resolves anisotropic direction and environment resources. Recording, CPU and wgpu do not execute PBR shading.
The broader PBR branch's historical GPU qualification is not inherited here.

### Reuse and correctness controls

A second static render keeps one native capture and one native plan. PBR factor
edits originally requested conservative recapture. C16 adds qualified state-only
PBR factor replay; resource/role and unsupported dependency changes still recapture. Qualified single
in-scene projection edits update native/transport camera state with no additional
capture or planning. PBR instance grouping and the legacy camera lowering shortcut
are disabled until their PBR uniform/normal dependencies are qualified.

`SoRetainedPBRTest` links **Coin only** and covers native ordinary/compact capture,
shared ownership, equality/interning, recapture, invalid factors, overrides,
separator restore, callback state, bindings and `.iv` loading.
`CoinRenderRetainedPBRBoundaryTest` retains the Recording refusal/publication test.

`CoinRenderRetainedPBRGpuTest` runs the current and retained fixtures serially.
Both use the same scene source, camera and Vulkan options. The campaign also
compiled that fixture against an unmodified archive/build of the pinned PBR
branch, providing a third control independent of this branch's transport changes.
The matrix contains 54 camera/light/metallic/roughness configurations, with
nonuniform transformed geometry, clipping, HDR emission and clearcoat. Initial,
material-edit and projection-edit states produce 162 frames and 324 RGBA/depth
buffers per path. All three paths are byte-identical. Static reuse counters and
refusal of invalid anisotropy factors or BASE_COLOR state with previous pixels/serial preserved are asserted separately.

The first diagnostic run exposed one-ULP depth differences from using D32F in the
new HDR target while the original PBR target used D24S8. Preserving the original
format policy removed them; comparison tolerances were not changed. These finite
fixtures do not establish universal cross-device equivalence or a performance gain.
No Android or million-building PBR campaign was run.

Run the automated current-vs-retained comparison:

```sh
cmake --build /path/to/bgfx-build --target CoinRenderCurrentPBRCompare CoinRenderRetainedPBRCompare
ctest --test-dir /path/to/bgfx-build -R '^CoinRenderRetainedPBRGpuTest$' --output-on-failure
```

The [serial comparison runner](../scripts/coinrender/run_retained_pbr_comparison.py)
also accepts `--reference /path/to/pinned-pbr-fixture` to include the source-branch
control, or `--compare-existing` to audit saved output directories. It checks exact
payload lengths and complete inventories, and fails on any byte difference.
[Validation receipts](validation/coin-render-retained-pbr-bgfx-20261028/results.json)
record the source revision, fixture/shader/library hashes and comparison results.

### Output exposure and real 3D examples

`CoinRenderOptions::pbrExposureStops` and
`CoinRenderTarget::setPbrExposureStops(float)` accept finite stops in [-16,16].
Exposure multiplies only the HDR output transform; the material/light inputs and
scene depth remain unchanged. Updating it publishes nothing until the next render.
Invalid values preserve the configuration and last publication. This is C9
CoinRender output policy; no exposure state or GPU type was added to native IR.

```cpp
// After a successful retained render with this target:
target->setPbrExposureStops(2.0f); // Four times the tone-mapper input.
renderer.render(root, *target); // Same native capture, plan and transport.
```

The [gallery example](../examples/coinrender/coin_render_retained_pbr_gallery.cpp)
contains two real Coin scenes: a twelve-sphere material grid and a four-part CAD
view using Cube, Cylinder, Sphere and Cone. Both use three directional lights,
opaque PBR factors and clearcoat, with no environment/IBL or shadows. Rows in the
material grid show a blue dielectric, a gold-colored metal and red clearcoat;
columns use roughness 0.08, 0.25, 0.50 and 0.85. The clearcoat lobe's own roughness
stays 0.15 while the substrate roughness varies.

At 1200 × 840, each scene renders exposure 0 → -2 → +2 → 0 stops. Color changes
and restores exactly, depth stays byte-identical, and the retained counters stay
at one capture, one plan and one transport build. Rejected NaN/infinite/out-of-range
setters preserve output and configuration. All twelve buffers match both the
current callback path and an independently compiled gallery linked to the pinned
`codex/pbr-clean` build. These examples qualify that finite profile, not IBL,
texture PBR or performance on Android.

```sh
cmake --build /path/to/bgfx-build --target CoinRenderRetainedPBRGallery CoinRenderCurrentPBRGallery
ctest --test-dir /path/to/bgfx-build -R '^CoinRenderRetainedPBRGalleryTest$' --output-on-failure
python3 scripts/coinrender/render_retained_pbr_gallery.py \
  --input /path/to/bgfx-build/testsuite/coinrender/retained-pbr-gallery/retained \
  --output /tmp/pbr-previews
```

The preview script requires Pillow and DejaVu Sans fonts. It saves original-size
PNGs and composes labeled previews from actual readbacks; the exposure strip uses
one fixed crop of unused CAD background for all three exposures. It does not
synthesize shading or modify source RGB values before display resizing.
With `COIN_BUILD_EXAMPLES=ON`, the standalone example target is
`coin_render_retained_pbr_gallery`; it takes an existing output directory.

[Gallery comparison and source receipts](validation/coin-render-retained-pbr-gallery-20261028/results.json),
[material preview](validation/coin-render-retained-pbr-gallery-20261028/materials-gallery.png)
and [exposure preview](validation/coin-render-retained-pbr-gallery-20261028/exposure-gallery.png)
are retained with the raw-buffer hashes. Earlier receipts remain historical.

### Global isotropic IBL

The retained BGFX path now consumes the pinned PBR branch's global environment
model: a prefiltered GGX cube, DFG/albedo LUTs, SH9 irradiance, energy compensation
and specular normal-variance filtering. The `COIN_PBR_IBL=1` fragment and lookup
helpers are reused from `codex/pbr-clean`; the normal native capture and planner
still produce the only scene frame and order.

`CoinEnvironment` is a separate CPU library linking **Coin only**. Its imported
package reader, validator, baking/math and source interfaces contain no renderer
or GPU dependency. This optional resource module is candidate F10; it is not a
new native IR field or an automatically upstream-ready contribution. OpenEXR is
not enabled in this port. The current validation covers version-1 procedural
`.cibl` packages and the imported CPU package/math regression.

C10 owns the target's `shared_ptr<const CoinEnvironment>` and BGFX resources.
The target reads and validates its source once; replacing or deleting the file
after that does not change the live snapshot. The CPU object survives executor
retirement, while handles belong to the executor. This step does not expose the
source branch's transactional bundle-replacement API: select another target for
another package. Scalar rotation/intensity changes reuse the existing snapshot.

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.pbrEnvironmentBundle = "studio.cibl";
auto * target = CoinRenderTarget::createOffscreen(SbVec2i32(1200, 840), options);
renderer.render(root, *target);
target->setPbrEnvironmentParameters(1.57079632679f, 0.5f); // +90 degrees, half intensity
renderer.render(root, *target); // No new native capture, plan, transport or IBL upload.
```

Rotation is in radians about world +Z; intensity is finite in [0,64]. Invalid
parameters preserve options, pixels and submission serial. A rigid camera is
required. Package layout/checksum/semantic validation precedes texture upload;
invalid input is refused with the previous publication preserved. A cold corrupt
package can be corrected and retried before an immutable snapshot is accepted.
GPU admission checks the actual specular/LUT/average texture footprint against
`pbrEnvironmentBudgetBytes` (default 128 MiB). CPU packages retain the imported
64 MiB payload ceiling. These are allocation budgets, not physical GPU-memory
measurements. The original source cube remains in CPU storage but is not uploaded
by this surface-only profile, since HDR background rendering is not connected.

At the C10 stage the connected profile remains native Vulkan offscreen, opaque isotropic PBR and
one sample/pass. IBL can coexist with the existing direct-light shader loop.
Anisotropic source arrays, local probes, environment background/skybox,
reflection/AO/refinement and shadows remain outside this connection. It does not
enable IBL shading in Recording, CPU or wgpu.

The comparison uses two 128-face/128-LUT procedural packages, `studio` and
`asymmetric`, with the same cameras/materials/geometry and no direct lights.
For each package, two 1200 × 840 scenes exercise base lighting, +90-degree
rotation, quarter intensity, zero intensity, +2-stop exposure and exact restore.
The five saved states give 20 RGBA/depth buffers per package, **40 total**;
current callback capture, retained capture and the unmodified pinned-source build
are byte-identical. The source control explicitly selects its non-array isotropic
profile (`pbrAnisotropicEnvironmentSamples=0`); no authored anisotropy is used.

Fixtures assert one native capture, one plan, one transport build and one IBL
upload per scene throughout scalar edits. Depth is unchanged, intensity zero
creates no light, and rendering succeeds after deleting the loaded fixture copy.
A separate test verifies corrupt-input publication preservation, retry, frozen
source reuse and rejection of an insufficient GPU environment budget. The optional
SDK export configuration was checked; that is not a complete installation test.
These are finite-profile results, not Android/million-building measurements or
universal adapter/driver qualification. No performance improvement is claimed.

```sh
cmake --build /path/to/bgfx-build --target coin_render_ibl_baker CoinRenderCurrentIBLGallery CoinRenderRetainedIBLGallery
ctest --test-dir /path/to/bgfx-build -R '^CoinRenderRetainedIBL' --output-on-failure
/path/to/bgfx-build/bin/coin_render_ibl_baker studio /tmp/studio.cibl
python3 scripts/coinrender/run_retained_ibl_comparison.py \
  --current /path/to/bgfx-build/bin/CoinRenderCurrentIBLGallery \
  --retained /path/to/bgfx-build/bin/CoinRenderRetainedIBLGallery \
  --environment /tmp/studio.cibl --output /tmp/ibl-studio
```

The runner accepts `--reference` for a fixture linked to the pinned PBR build.
The public standalone example is `coin_render_retained_ibl_gallery` when examples
are enabled; it takes an existing output directory and a `.cibl` filename.
[Receipts and origin hashes](validation/coin-render-retained-ibl-20261028/results.json),
[IBL materials](validation/coin-render-retained-ibl-20261028/ibl-materials-gallery.png)
and [environment/rotation examples](validation/coin-render-retained-ibl-20261028/ibl-environments-gallery.png)
are saved. Labeled previews come from actual readbacks; only the CAD strip crops
unused background and resizes the same rectangle for display.

### FreeCAD geometry smoke test

The local FreeCAD App/Part Python modules generated a valid bored mounting
bracket, saved as an FCStd document, then tessellated it into 1,520 vertices and
3,056 triangles. The exported plain Inventor mesh received an explicitly authored
metallic PBR material and the studio IBL. It does not import FreeCAD appearance.
At the same 1200x840 camera, the current callback and native retained BGFX paths
produce ten byte-identical color/depth buffers over five saved lighting/output
states. Restoration and invalid setters also pass. All six retained renders keep
one capture, one plan, one transport build and one environment upload.

[Fixtures, preview, statistics and results](validation/coin-render-freecad-ibl-20261028/results.json)
record this geometry interoperability test. It uses FreeCAD without its GUI;
it does **not** qualify the native viewport, custom FreeCAD nodes, live edits,
selection or picking, and makes no performance or memory claim. The local
FreeCADGui resolves its bundled Coin library, while this test executable resolves
our retained build. QuarterWidget's existing local manager adapter still needs
retained PBR/IBL integration and a supported presentation path before that
viewport can be tested. No FreeCAD library was replaced.

The follow-up [AssemblyExample test](validation/coin-render-freecad-assembly-ibl-20261028/results.json)
opens the actual Assembly workbench and records its OpenGL viewport in an isolated
X11 display. A macro exports the viewport camera and the 13 assembly link shapes
(43,401 triangles), hiding the separate source-parts group to avoid duplicate
geometry. The macro retains source RGB colors and authors uniform metallic 0.65,
roughness 0.35 test parameters. A separate BGFX test loads that mesh and exact
camera: ten color/depth buffers match the current callback control byte for byte;
all six retained states keep capture/plan/transport/environment-upload counts at
one. This is not a comparison against FreeCAD OpenGL shading or a native-window
PBR test. Joint movement, selection/highlighting and performance remain untested.
The accompanying FCMacro, exported scene, raw buffers, GUI screenshot and PBR
previews make the distinction reproducible. The successful GUI launch explicitly
uses the installed PySide6 Qt libraries to avoid its mismatch with system Qt.

### Readback FreeCAD Assembly viewport prototype (C11)

The C11 prototype introduced retained BGFX PBR/IBL pixels in the active FreeCAD 3D
area. The [interactive macro](../examples/coinrender/freecad_retained_viewport.FCMacro)
now defaults to native presentation (C12 below); explicit `presentation="readback"`
on `attach()` retains this C11 mode as a control.
The FreeCAD build's Coin and this branch's Coin have different ABIs; an isolated
worker avoids replacing either library or passing Coin pointers between them.
This is **Vulkan offscreen readback to a Qt widget**, not a native BGFX swapchain
or an extension of QuarterWidget's older manager adapter.

```text
FreeCAD BRep links --one initial tessellation--> stable Coin nodes in worker
FreeCAD camera / Placement / visibility / selection --> changed node fields
                         |
              SoIRRenderAction / retained updater
                         |
                 single SoRenderPlanner
                         |
             retained transport -> BGFX PBR + IBL
                         |
                   readback -> Qt viewport
```

Only the worker captures and plans rendered pixels. The original FreeCAD graph
remains available to its picking action and selection model. Its render manager
is deactivated, and its native viewport paint is disabled while this mode is
attached. A read-only `SoCallback` audits actual `SoGLRenderAction` visits; the
GUI test requires zero such visits during retained rendering and verifies that
OpenGL rendering resumes after detaching. Picking is not a second render capture.
The callback capture control is replayed in a separate process after the GUI
exits; it is never invoked by the interactive connector.

The adapter imports each rigid link mesh once, retains its source RGB color,
and authors the demo metallic/roughness factors 0.65/0.35. Frame messages contain
camera, rigid poses, visibility and interaction flags; they contain no geometry.
Translation uses `SoTranslation` and the existing native incremental updater.
Rotation uses `SoRotation` and conservatively recaptures once. Object highlight
is authored emissive PBR state in that same capture, with selection taking
priority over hover. Material changes conservatively recaptured once in C11; C16 adds scalar replay. Static and
IBL scalar changes reuse capture/plan/transport; no second planner is introduced.
Resize keeps picking disabled until the matching image arrives. Pending document
or camera changes are also published before another click can pick stale pixels.

Left click selects through `FreeCADGui.Selection`; hover preselects. Right drag
rotates the camera, middle drag pans, wheel zooms and F fits. Edit a link's
Placement in FreeCAD to update its position/orientation live. Escape restores the
original viewport. Normal sessions discard old frame files and bound diagnostic
history; the explicit `record=True` test mode retains replay evidence.

This is an Assembly-link profile: uniform opaque materials, object-wide highlight
(including when a face is selected), global isotropic IBL and rigid placements.
Native face/edge highlight styling, joint/selection gizmos, arbitrary custom
FreeCAD nodes, scaled links, transparent/textured surfaces and topology edits are
not qualified. A source shape/relink/profile change exits the mode and requires
reattachment instead of silently keeping stale geometry. C16 adds optional PBR maps; skyboxes,
shadows and other executors remain outside the existing C9/C10 profile. Readback,
IPC and Qt copies have a cost; this campaign does not claim performance or memory
gains. No FreeCAD source, installed Coin, or FCStd file is overwritten.

Build `CoinRenderFreeCADViewportWorker` with the BGFX test configuration, or
`coin_render_freecad_viewport` with the BGFX render examples enabled. Prepare an
IBL package with `coin_render_ibl_baker`. Set `COIN_VIEWPORT_SOURCE`,
`COIN_VIEWPORT_WORKER` and `COIN_VIEWPORT_ENVIRONMENT` before launching FreeCAD with
the interactive macro. `COIN_VIEWPORT_ASSEMBLY` optionally opens an FCStd first.
The complete test runner is [run_freecad_retained_viewport.py](../scripts/coinrender/run_freecad_retained_viewport.py):

```sh
python3 scripts/coinrender/run_freecad_retained_viewport.py \
  --freecad /path/to/freecad-build/bin/FreeCAD \
  --worker /path/to/coin-build/bin/CoinRenderFreeCADViewportWorker \
  --environment /path/to/studio.cibl \
  --assembly /path/to/AssemblyExample.FCStd --output /tmp/fresh-viewport-run --presentation readback \
  --qt-root /path/to/PySide6/Qt --xvfb
```

`--qt-root` is only needed when the installed PySide6 and system Qt differ. The
runner uses a temporary FreeCAD configuration, drives native Qt mouse events,
and compares every published color/depth buffer against callback capture using
the exact recorded requests.

The [viewport campaign receipts](validation/coin-render-freecad-retained-viewport-20261028/results.json)
record 18 GUI stages, 38 byte-identical color/depth buffers and 11 existing
regression controls. In addition to clicks, hover, rigid translation/rotation,
visibility, IBL parameters, camera and resize, the campaign moves a piece,
explicitly runs FreeCAD's Assembly solver and recomputes the document, then
restores all original placements. Translation reuses capture/plan/geometry;
rotation, interaction materials and multi-pose solver changes recapture once
as required. Every frame has at most one capture and one plan build. The IBL
upload count stays one and all meshes are tessellated only once per attachment.
Lifecycle controls cover normal detach with actual GL rendering resumed,
interactive frame-file disposal, source-shape rejection and worker/session cleanup
after document deletion. Screenshots show real BGFX pixels in the FreeCAD window,
including the selected link in its tree. The AddonManager startup warning about
missing `defusedxml` is unrelated to the passing Assembly test; the source-edit
rejection diagnostic is intentional. Source/worker hashes, dependency resolution,
recorded requests, raw readbacks and reproduction commands accompany the results.

### Native FreeCAD viewport presentation (C12)

The default connector now creates a native QWidget child window in the active
FreeCAD view and passes its XID to the isolated worker. The worker opens its own
connection to that X server and creates a `CoinRenderTarget::createWindow()`
target. Neither Coin pointers nor Xlib `Display*` values cross processes. This
profile is qualified on Linux/X11 with Qt's xcb platform and BGFX/Vulkan.

```text
FreeCAD meshes / camera / selection / poses
                -> stable Coin nodes in one worker
                -> SoIRRenderAction / native updater
                -> one SoRenderPlanner
                -> retained transport / BGFX PBR + IBL
                -> HDR / Neutral output directly to the native swapchain

worker -> Qt: acknowledgements and statistics only
```

Native Vulkan PBR now admits the existing window target while keeping the
single-sample, single-pass opaque factor/clearcoat/IBL restrictions. Its existing
HDR and tone-mapping shaders write to the native output framebuffer. An empty
BGFX resource frame fences completion before interaction acknowledgements; it
does not replay scene draws or request a screenshot. No second capture/planner
or fullscreen CPU image composition is involved.

In native mode the worker never calls color/depth readback, never writes pixel
files, and requires empty target CPU color/depth storage and no window screenshot
request. Qt allocates no viewport pixel image and never paints BGFX pixels. The
native widget handles input/layout/exposure; an exposure republishes the same
retained scene. Framebuffer extents and picking metadata are updated together,
including after resize. Hidden/minimized views do not enqueue new native draws.
There is no automatic readback fallback; the old mode requires explicit selection.

The original QOpenGLWidget is hidden as well as paint-disabled, so Qt cannot
recompose its stale GL texture. FreeCAD floating dock-overlay containers
intersecting the view are temporarily masked because their transparent areas
cache that old GL image. Docked tree/property panels remain usable. Exact overlay
masks and the original viewport are restored on detach. Native floating-overlay
and gizmo composition remain outside this bounded profile; this workaround does
not change FreeCAD preferences or save the document.

The native target must retire before Qt destroys its XID. Ordinary detach is
asynchronous and restores the old viewport only after worker exit. FreeCAD's Gui
deleted-document signal runs before its views are destroyed, so it fences worker
retirement there; the App notification is too late for this purpose. View close,
application quit and platform-surface teardown are also handled. Reattachment
waits for the previous worker. A bounded termination fallback exists, but the GUI
qualification requires normal exit code zero for detach, profile exit and document
deletion, with the window still valid at the document-retirement fence.

The native runner uses the same command as C11 with `--presentation native`
(the default). Its test-only OS surface snapshots are captured after presentation,
saved for comparison, and never supplied back to Qt for display. Separate worker
processes replay the recorded messages after the GUI exits: callback capture and
retained capture both render offscreen controls. Native presented color must match
the controls byte for byte, while the two offscreen paths also compare depth.
No native-window depth readback is claimed. The campaign covers pointer selection,
preselection/object highlight, translation, rotation, solver/recompute, visibility,
IBL, camera, resize, post-resize picking and native-surface lifetime.

This is directly related to the retained stack authored by tritao/Joao Matos: the
pinned upstream commit `17a9e4b21` records that author and `joao@tritao.eu`. We reuse
its `SoIRRenderAction`, retained updater and `SoRenderPlanner`, with the locally
documented semantic adaptations. PBR/IBL comes from the local `codex/pbr-clean`
connection. Native BGFX swapchain output, process isolation, FreeCAD ingress/input
and host-window lifetime are our C11/C12 integration work. C12 adds no native IR
or planner state and is not an upstream fork patch. The FreeCAD application still
uses its bundled Coin for its document/view/picking machinery; the retained worker
uses this branch's Coin. This qualification does not turn arbitrary FreeCAD custom
nodes, face/edge highlight styles, scaled/transparent links or topology edits into
supported retained semantics, and makes no FPS or whole-process memory claim.

The [native viewport receipts](validation/coin-render-freecad-native-viewport-20261028/results.json)
record 19 GUI stages, 19 byte-identical native surface images, 40 byte-identical
offscreen control color/depth buffers and 11 existing regression controls. The
native worker's 20 submissions use one XID across resize and report zero pixel
readbacks, zero CPU pixel transport, zero Qt image bytes and zero source-scene GL
render visits. The three lifecycle cases exit normally; the Gui document fence
confirms worker retirement while the native XID is still valid. The previous
readback mode also passes 19 stages and 40 exact buffers as an explicit control.
Raw native OS snapshots, offscreen control buffers, scene/requests, dependency
resolution, source/binary hashes, fork author provenance and reproduction commands
are included. Test snapshots never participate in presentation.

### Assembly appearance example (C13)

The [appearance macro](../examples/coinrender/freecad_assembly_appearance.FCMacro)
opens a closer perspective view of AssemblyExample and applies proposed materials
per link through `attach(..., material_overrides=...)`. Overrides accept only
linear `baseColor`, `metallic`, `roughness`, `clearcoat` and
`clearcoatRoughness`; unknown links/fields and non-finite/out-of-range values are
rejected before UI takeover. Omitted overrides keep the original C11/C12 defaults.
No ViewObject appearance, shape or FCStd data is saved or overwritten.

| Components | Proposed appearance | Metallic | Roughness | Clearcoat |
| --- | --- | --- | --- | --- |
| Boom, Stick | Yellow painted metal | 0 | 0.28 | 0.55 |
| Base, cylinder bodies | Graphite paint | 0 | 0.30 | 0.35 |
| Bucket and links | Exposed steel | 1 | 0.38 | 0 |
| Cylinder rods | Polished metal | 1 | 0.12 | 0 |
| Base pin | Steel pin | 1 | 0.22 | 0 |

These are visual authoring choices, not an imported bill of materials. Paint is
treated as dielectric rather than assigning one intermediate metallic value to
every object. The framing helper queries only assembly-link bounds, excluding
loose source parts and joint bounds; it does not invoke a second render capture.
The camera uses perspective to reveal changing view/reflection directions across
flat faces. The initial studio IBL uses rotation 0.6 rad, intensity 0.85 and
exposure -0.25 stops. Two additional IBL rotations and a cylinder detail view are
provided. Native BGFX presentation and the same capture/planner pipeline remain.

[Appearance receipts and previews](validation/coin-render-freecad-assembly-appearance-20261028/results.json)
include the original uniform material and industrial preset at exactly the same
camera, light and geometry. Their offscreen depth is byte-identical. Five actual
native surface snapshots are saved, with zero pixel readbacks/CPU image transport
in the renderer. The three industrial light variants retain one capture, plan,
transport and environment upload; the detail camera conservatively recaptures
once. Callback and retained offscreen controls match byte for byte for all five
views. Native versus offscreen is not byte-identical for these perspective
closeups: measured mean absolute 8-bit channel error is about 0.0034–0.0047, with
larger localized differences recorded. Their cause is not investigated in this
appearance change, and this campaign does not extend the prior orthographic
byte-equivalence claim. No geometry, shader, shadow/AO or texture feature is added.

Use the C12 environment variables and launch the appearance macro to leave it
interactive. Setting `COIN_ASSEMBLY_PREVIEW_OUTPUT` to a fresh directory instead
captures variants and exits, for reproducible visual qualification. Set
`COIN_ASSEMBLY_EFFECTS_PREVIEW=0` to reproduce the five material-only C13 variants;
the default now shows the C14 effects below.


### Native PBR shadows, contact occlusion and background (C14)

The native viewport now connects directional shadows and ambient occlusion to
PBR/IBL. The [Assembly appearance macro](../examples/coinrender/freecad_assembly_appearance.FCMacro)
shows them by default with a neutral blue-gray gradient and a presentation floor.
It authors one directional key light and the analytic floor in the same worker
root as the imported links. Neither becomes a FreeCAD document object or changes
saved CAD geometry. The standard native viewport macro also enables this studio
setup. The reusable `attach()` API keeps effects and studio geometry opt-in:

```python
renderer = freecad_retained_viewport.attach(
    worker, environment, material_overrides=materials, studio_setup=True,
    effects=dict(shadows=True, ambientOcclusion=True, background=True,
                 shadowMapSize=2048, shadowBias=.00015,
                 occlusionRadius=18., occlusionStrength=.9))
renderer.effects.update(shadows=False, ambientOcclusion=False)  # same capture/plan
```

`CoinRenderTarget::setPbrEffects()` changes executor settings only. Its input
validation is transactional: invalid values preserve the previous settings and
publication. Protocol `FX` is optional, so earlier worker frame messages remain
valid with effects disabled. Background colors are sRGB values in [0,1] and
remain independent of exposure. AO radius is in scene/world units for the rigid
camera profile; applications must choose it for their own unit scale.

The executor consumes the supplied captured lighting, matrices, indexed geometry,
GPU vertex/index buffers and draw plan. It fits an orthographic shadow volume to
that geometry, renders first-directional-light depth, and applies 3x3 PCF only to
that light's direct radiance. Receiver-plane correction compares taps at sampled
texel centers to prevent sloped-surface self-shadow patterns. A second depth pass
from the same camera supplies deterministic 16-sample screen-space AO, applied
to indirect IBL and its clearcoat contribution. These are extra GPU geometry
passes, not additional Coin traversals, captures or semantic render plans. Native
presentation remains a Vulkan swapchain with no CPU pixel transport to Qt.

Depth resources are cached against the trusted immutable transport borrow,
target dimensions and shadow map size. Static frames, effects toggles, background
colors, radius/strength/bias and IBL scalar edits reuse compatible depths. A new
scene publication invalidates them conservatively; incremental translations thus
refresh shadows/AO without recapturing or rebuilding the upstream plan. The
executor also uses camera-patched draw matrices when available. Selection material
and camera changes retain the existing conservative single recapture behavior.
Resources stay resident when effects are disabled to permit reuse and are freed
with the target. `resourceBudgetBytes` bounds resident plus candidate depth FBO
allocations (currently depth-only D24S8, four nominal bytes/pixel),
separately from existing HDR/IBL/geometry budgets. C14 initially used 32 MiB for
the 2048 shadow map and about 8.34 MiB for AO at 1234x886. C15 below removes the
dummy color attachments: the current sizes are 16 MiB and about 4.17 MiB,
respectively. These are nominal attachment sizes, not measured process or driver
memory.

This bounded profile requires native Vulkan, opaque PBR triangle surfaces, one
full-target viewport and rigid camera, ordinary writable LESS/LEQUAL depth, and
shared lighting whose first light is directional for shadows. Clipping planes,
polygon offset, mixed/non-PBR surfaces, transparent/alpha-tested geometry,
textures, MSAA and native `SoShadowGroup` remain unsupported here. SSAO sees only
camera-visible depth; it is not geometry-ray AO, offscreen occlusion, full global
illumination or the BVH AO implementation from `codex/pbr-clean`. This new PCF/SSAO
executor coupling does not claim new upstream fork semantics. Other executors
explicitly reject active settings rather than silently ignoring them.

[Validation receipts](validation/coin-render-freecad-pbr-effects-20261028/comparison.json)
record nine actual native previews, including controlled effects-off,
shadows-only, AO-only, both, restored-off, detail, selection, movement and
restoration. The first five share scene, camera, lighting and geometry; capture,
plan, transport and environment-upload counters stay at one. Each depth pass
runs once and is reused when both effects are enabled. Translation refreshes
both depths through the existing incremental update. Toggling effects off and
restoring interaction reproduces the corresponding native images byte for byte.
Native renderer readback count, CPU image bytes, Qt image bytes and source GL
render visits remain zero. The OS screenshots are test evidence only.

`CoinRenderPbrEffectsTest` verifies perspective and orthographic shadows/contact
occlusion, unchanged published depth, static cache reuse, incremental movement,
background exposure invariance, exact disabled restoration, invalid settings,
resource-budget rejection preserving the last image, and missing-light admission.
Offscreen callback and retained controls replay the same GUI requests in separate
processes. The comparison receipt records their exact buffer hashes and the
measured native-versus-offscreen differences; the latter retain the C13 perspective
rasterization limitation and are not claimed byte-identical. Existing PBR/IBL,
admission, retained connector and BGFX Core/instancing controls also pass.

Reproduce with the C12 environment variables plus
`COIN_ASSEMBLY_PREVIEW_OUTPUT=/tmp/fresh-effects-preview` and launch the appearance
macro. After it exits, run:

```sh
python3 scripts/coinrender/compare_freecad_pbr_effects.py \
  --worker /path/to/CoinRenderFreeCADViewportWorker \
  --environment /path/to/studio.cibl --preview /tmp/fresh-effects-preview
ctest --test-dir /path/to/coin-build -R '^CoinRenderPbrEffectsTest$' --output-on-failure
```


### Static native PBR reuse and measured effects cost (C15)

The executor now retains a receipt for the completed PBR HDR radiance already
stored in its existing framebuffer. An unchanged native repaint resolves that
radiance to the swapchain, without lowering/uploading/drawing scene geometry or
repeating direct lighting, IBL, PCF or SSAO. It creates no additional image cache.
Offscreen controls continue to render normally. Native normal idle behavior was
already event-driven: if nothing requests a repaint, the connector sends no new
frame. The benchmark below explicitly forces repaints; it does not claim an idle
FPS gain or measure displayed FPS.

Reuse requires a trusted lexical immutable `CoinRenderFrameBorrow`, matching
options, target dimensions/resource generation and a committed publication
serial. A caller-provided revision alone cannot prove reuse. The old receipt is
revoked before admission; rejection, resize, teardown, device loss, changed
camera/pose/material/visibility/clear or changed PBR settings require a new render.
This remains one fork capture/updater/planner and one transport. Native object
translations still update incrementally. C15 left material/camera recapture
rules unchanged; C16 below adds qualified scalar material replay. Output-only
changes conservatively redraw too.

The unchanged logical image is already published, so its repeat resolve/present
uses normal BGFX frame backpressure without another empty resource-frame fence.
Changed scenes keep the C12 renderer-thread completion boundary before their
acknowledgment. Static acknowledgment can precede execution of the queued repeat
presentation; it never acknowledges a new camera or image state through this
shortcut. Shutdown drains that work before the host releases its native XID.
The native tests explicitly close the worker after a queued static repaint and
verify normal retirement. Readback requests bypass reuse. Phase tracing bypasses
reuse to retain the existing full-frame profiling path;
`COIN_BGFX_DISABLE_PBR_PRESENT_REUSE=1` is an explicit diagnostic control.

Both effect depth FBOs now contain only D24S8, eliminating the unused RGBA8
attachment and its clear. PCF/SSAO sample attachment zero; the depth image and
shading output are unchanged. Resident-plus-candidate budget accounting and
public attachment counters use four nominal bytes/pixel. Nine actual native
AssemblyExample snapshots match the C14 output byte for byte, including effects,
selection, movement and restoration.

The [measurement receipts](validation/coin-render-freecad-pbr-static-20261029/comparison.json)
use actual FreeCAD processes with the native Qt/Xlib viewport on an NVIDIA
GeForce RTX 3060 Laptop GPU (Vulkan; driver 615.71.09), Ryzen 7 5800H and Linux
6.17.0-42. The scene includes the same 13 links, proposed PBR materials, studio
key light/floor, perspective camera, 1234x886 extent, IBL rotation/intensity and
exposure. Scene, camera, pose-packet, FCStd and environment hashes are checked.
Each effects configuration runs in three fresh FreeCAD/worker processes, with
8 warmup repaints and 40 measured contiguous repaints per process: 120 samples
per configuration and version. The order is deterministic and mixed across
repetitions. No screenshots, readbacks, memory sampling or artificial sleeps occur
inside a timed render. Driver caches are not purged.

| Effects | Static submit median before -> after (ms) | P95 before -> after (ms) | Effect attachments before -> after (MiB) |
| --- | --- | --- | --- |
| Off | 31.15 -> 27.64 | 41.41 -> 28.25 | 0 -> 0 |
| Shadows | 31.13 -> 27.73 | 33.21 -> 28.57 | 32 -> 16 |
| SSAO | 32.10 -> 27.59 | 35.87 -> 28.24 | 8.34 -> 4.17 |
| Both | 30.84 -> 28.04 | 33.75 -> 28.48 | 40.34 -> 20.17 |

These are worker wall times for render/submit and renderer-thread backpressure,
not pure GPU execution, display latency or a measured refresh rate. Both-effects
static submission improved about 9%; improvements across modes were about
9–14%. The old presentation/fencing path dominated these times, so the initial
four modes do not show a consistent wall-time penalty for SSAO. The new static
path skips scene shading after its first frame: each process reports one shaded
frame and 48 reused repaints, one capture, one plan, one transport and one IBL
upload. A cached repaint still resolves/presents pixels on the GPU.

Separate diagnostic GPU timestamp probes force full scene shading with phase
tracing (10 steady samples/configuration; the additional query drain is excluded
from the normal benchmark). Median GPU frame time is 0.326 ms off, 0.349 ms with
shadows, 0.536 ms with SSAO and 0.566 ms with both. Thus both effects add about
0.240 ms when a changed scene really needs shading on this GPU. This probe does
not measure the cached resolve path or establish performance on other hardware.

`/proc/PID/smaps_rollup` is sampled after warmup and after the measured interval,
separately for FreeCAD and its worker. Worker PSS remains about 218–219 MiB and
RSS about 292 MiB across all modes/versions; with both effects it is 218.58 ->
218.64 MiB PSS. FreeCAD plus worker is roughly 703.60 -> 704.46 MiB PSS with both,
within the observed run variation; there is no demonstrated process-RAM saving.
The attachment reduction is 50%: BGFX total render-target estimates with both fall
from 52.85 to 32.68 MiB, of which effects account for 40.34 -> 20.17 MiB. BGFX
counts are nominal resource estimates, not measured dedicated-VRAM residency;
they must not be added to process PSS/RSS. Raw driver heap estimates are retained
only as diagnostics. The off row uses a fresh target that never enabled effects;
disabling effects on an existing target retains its depth resources for reuse
until target retirement/replacement. Swap was zero. Warm-to-end worker PSS variation was below
0.15 MiB per run; this short campaign is not a long-duration leak test.

First worker render/submit is recorded separately: about 527 -> 490 ms with
both effects (roughly 0.5 s across modes). It includes initial retained capture,
plan, transport and renderer resource preparation, but excludes FreeCAD startup,
FCStd load and BRep tessellation. Caches are warm at the driver/system level, so
this is not a clean shader-cache startup measurement, nor is that small cold-call
change isolated as an optimization result.

Validation also records 15 direct native protocol checks for static reuse,
exposure/bias/radius/background invalidation, budget rejection preserving the
publication serial, retry, resize, selection, incremental movement and shutdown
after queued presentation. The existing 19-stage FreeCAD GUI campaign passes
with 19 exact native/control images and 40 exact offscreen buffers. The effects
appearance campaign verifies cached pixels directly, retains nine previews and
compares 20 offscreen color/depth buffers exactly against callback capture.
Thirteen existing/new regression tests pass. Perspective native/offscreen
differences retain the documented C13 limitation; the nine before/after native
optimization images themselves are identical.

Reproduce with [benchmark_freecad_pbr_effects.py](../scripts/coinrender/benchmark_freecad_pbr_effects.py):

```sh
python3 scripts/coinrender/benchmark_freecad_pbr_effects.py \
  --freecad /path/to/FreeCAD --worker /path/to/CoinRenderFreeCADViewportWorker \
  --environment /path/to/studio.cibl --assembly /path/to/AssemblyExample.FCStd \
  --output /tmp/fresh-effects-benchmark --repeats 3 --samples 40 --warmup 8 \
  --expect-static-reuse --qt-root /path/to/PySide6/Qt
```

The runner starts Xvfb and uses fresh app configurations/directories per process.
Omit `--expect-static-reuse` for an older control executable. The frozen measured
baseline binary/library hashes and original/final receipts are included; its
local copy remains in `/tmp/coin-effects-baseline`. The direct native admission
runner is [test_native_pbr_reuse.py](../scripts/coinrender/test_native_pbr_reuse.py).
This work adds no fork IR/planner semantics and does not qualify the effects or
reuse shortcut on wgpu, Android, non-Xlib platforms or extended PBR profiles.

### Stored PBR maps and incremental materials (F11 / C16)

C16 connects the next subset of `codex/pbr-clean` at
`ce72805f0536f8f0ef07dc5575d301b3ddffa507`. Native capture stores logical
base/ORM/emissive images, a normal/height source and coordinate sets on units
0–7. Native IR contains no backend handle or CoinRender surface payload.
The adapter consumes that one draw list and supplied plan; shared Core converts
images and prepares MikkTSpace corner tangents without traversing the graph,
classifying draws or building another schedule. The reference callback capture
is used only in independent controls.

Base color and emission use sRGB conversion/mips; ORM and normal data stay
linear. RGB/RGBA normal images and L/LA height sources are supported, including
normal scale/Y flip, reflected model transforms and mirrored/degenerate UVs.
Source image alpha does not introduce transparency into this opaque profile.
Height conversion respects wrap/clamp. Conversion/mip scratch is bounded to
128 MiB. Tangent attributes are sparse optional storage; plain geometry keeps
its original vertex stride. Tangent corner expansion can still increase memory
for mapped meshes: no global memory-reduction claim is made here.

The native updater resolves effective PBR scalar values along affected state
paths and stops before the shape. It validates all candidates before replacing
immutable descriptors. Qualified base color, metallic, roughness, emission,
clearcoat, coat roughness, normal scale and normal Y flip edits preserve the
capture, plan, transport and geometry copies. Image/role/UV changes recapture;
geometry edits with prepared tangents rebuild surface transport. Connected
fields and mixed notification classes conservatively fall back to native
recapture. C17 adds anisotropy scalar replay when its direction was captured. None runs the old production capture as a fallback.

The FreeCAD adapter accepts optional maps per rigid link:

```python
renderer = freecad_retained_viewport.attach(
    worker, environment, assembly,
    surface_maps={"Boom": {
        "baseColor": "/absolute/base.png",
        "orm": "/absolute/orm.png",
        "normal": "/absolute/normal.png",
        "directionS": (.02, 0, 0), "directionT": (0, .02, 0)}})
renderer.material_overrides.setdefault("Boom", {}).update(roughness=.9, normalScale=0)
```

Maps/projection are attach-time authoring. The optional worker `PBR` packet
updates scalar factors and composes selection/hover emission with authored
emission. Existing packets without this block remain valid. This is an adapter
API, not a document material editor, automatic texture importer or image watcher.
It still exports each BRep mesh once to the isolated native worker.

The [qualification receipt](validation/coin-render-retained-pbr-surfaces-20261029/README.md)
contains the commands, pinned source, images and counters. Fourteen fixtures
produce 28 exact color/depth buffer comparisons between this branch's retained
and callback paths; ten update frames produce another 20 exact comparisons.
Against the pinned PBR SDK, the final run also has all 28 buffers exact. An
earlier run had one color channel differing by one 8-bit level on the
nonuniformly scaled sphere, depth exact. Its per-vertex tangent frame is resolved
in the shader rather than on the reference CPU path. That earlier result and
the runner's narrow one-channel allowance remain recorded explicitly.

The actual AssemblyExample viewport also accepts color/ORM/normal maps on Boom
and a height source on Bucket. Editing/restoring roughness and normal scale
keeps **one capture, one plan, one transport and 174381 copied vertices**;
restoration reproduces the original pixels. Twelve saved requests produce 24
exact offscreen retained/callback buffers after the GUI closes. Nine native
previews exercise detail, effects, selection and movement. The small map images
are conspicuous numerical test patterns, not proposed production materials.
Native presentation still reports zero Qt image bytes/pixel readbacks.

C17 below adds anisotropy/anisotropic IBL. Still outside the connected profile:
planar and rough local reflections,
probe allocation, geometry/BVH AO, stationary refinement, area lights/LTC and
penumbra passes, HDR skybox/dynamic environment, mixed Phong/PBR and transparent
PBR composition, MSAA/fog/stereo/layers. Some exist in the PBR branch but have
not been connected to this retained profile. PBR execution in wgpu and the
fork's GL executor, a complete glass model, and a persistent FreeCAD material
editor require additional implementation/qualification; they are not inherited
from this port. Separate `SoBumpMapCoordinate` overrides, border samplers,
scene-produced maps, cube/volume maps and native ShadowGroup semantics remain
explicitly unsupported. Candidate F11 must still be ported and tested against
the fork's native consumers before proposing upstream.

### Anisotropy and VNDF global IBL (F12 / F13 / C17)

C17 connects the authored anisotropy strength, rotation and tangent-coordinate
unit from the same pinned PBR branch. Direction uses its own texture matrix;
normal mapping additionally uses the bump matrix. Shared MikkTSpace computes
both frames after the one native capture, preserving the supplied draw order.
The native/callback coordinate bundle now requests UVs and corner indices for
PBR even when there is no color image. The old callback direct-geometry/cube
shortcuts defer to their ordinary primitive callbacks when they cannot represent
anisotropic corner coordinates; no second scene action or planner is invoked.

Immutable native surface data records whether directional coordinates were
captured. Strength and rotation edits, including disabling and re-enabling an
already captured lobe, use the native state-path updater. First enabling a lobe
on an initially isotropic scene without captured direction dependencies still
recaptures/replans once. Role/coordinate-unit changes and mixed notification
classes retain conservative invalidation. Camera/model transforms resolve the
same object-space tangent data in the BGFX vertex shader.

`CoinEnvironmentAnisotropy` is the optional renderer-independent CPU module from
the source branch: versioned directional/average albedo products, bounded
preparation/read, validation, atomic write and evaluation. C17 owns their target
snapshot, optional `.cani` path and GPU representation. The default table is
prepared once and shared immutably while another target retains it. The explicit
`pbrAnisotropyCpuBudgetBytes` covers this product/preparation independently of
the existing environment snapshot. It does not claim to bound all CPU memory.

BGFX/Vulkan admits native RGBA16F cube arrays: SOURCE radiance in layer 0,
GGX-prefiltered radiance in layer 1, plus packed albedo and average tables.
The GPU budget accounts for full allocated cube mip chains and LUT padding,
including coexistence during replacement. It never silently chooses the old
approximation on failure. `pbrAnisotropicEnvironmentSamples` defaults to 16,
is bounded to 128 and is tested at 16 and 64; zero explicitly selects the old
bent-reflection diagnostic profile. This is the pinned branch's interactive VNDF
ratio estimator, not stationary convergence or a local-probe implementation.
The distant-only shader excludes local-probe resource/dependency code.

```cpp
CoinRenderOptions options;
options.pbrEnvironmentBundle = "/absolute/studio.cibl";
options.pbrAnisotropicEnvironmentSamples = 16;
options.pbrAnisotropyProducts = "/absolute/albedo.cani"; // optional
// Prepare once: coin_render_ibl_baker anisotropy /absolute/albedo.cani
```

FreeCAD `material_overrides` now accepts `anisotropy` in [0,1] and finite
`anisotropyRotation` in radians. An optional `ANIS` block follows `PBR` in the
worker protocol. Old packets remain valid. A direction-only `surface_maps`
entry supplies planar tangent authoring without an image. The opt-in appearance
preview sets exposed steel surfaces to strength .85/roughness .32, retaining the
paint presets. `COIN_ASSEMBLY_ANISOTROPY_PREVIEW=1` records the comparison;
`COIN_ASSEMBLY_ANISOTROPY_DETAIL=1` uses the fixed bucket close-up camera.
These are appearance proposals, not imported or saved engineering materials.

[Qualification evidence](validation/coin-render-retained-pbr-anisotropy-20261029/README.md):
14 fixtures give 28 exact retained/callback **and pinned source SDK** color/depth
buffers, including direct-only, IBL-only, 0/16/64 sampling, mirrored/degenerate
UVs, separate bump transforms, unit 7 without images, spheres and nonuniform
models. Nine update frames give 18 exact retained/callback buffers, stable
capture/plan/transport/copy counters and rejection/retry checks. The imported CPU
suite compares 81 cases against independent double-precision NDF quadrature.

Two actual AssemblyExample campaigns use the same scene and a fixed camera per
campaign: overview and bucket detail. Each passes nine native GUI stages and 20
exact independent offscreen color/depth buffers. Strength/rotation, zero/restore,
selection and movement keep captures/plans/transports at one and copied vertices
at 202788. Restoring movement together with selection conservatively recaptures
once (cumulative counters two); restoration pixels remain exact. Forced static
repaints reuse HDR shading; all native requests report zero pixel readback/Qt
image bytes. The native screenshots are separately recorded: sparse raster-edge
differences remain versus offscreen controls, so those screenshots are not
claimed byte-identical. Their per-stage errors remain in the receipts.

The target retains one 1050624-byte CPU albedo product; its complete environment
GPU allocation is 3418080 bytes. Those are specific resource footprints, not
whole-process/GPU memory. On this machine the automatic table preparation took
about 4.1 seconds in the CPU control; a prepared `.cani` avoids recomputing it in
new targets/processes. Scalar edits neither regenerate the table nor re-upload
the environment. No Android or wgpu PBR performance claim is made.

C19/C20 below add smooth and bounded rough planar reflections. Remaining: local probes, stationary refinement,
geometry AO, area lights, HDR skybox/dynamic environment, transparent/glass and
mixed-material composition, broader sampling/layer profiles, wgpu/GL PBR and a
persistent FreeCAD material editor. F12/F13 still need a separately qualified
native fork port; the BGFX resource/shading work remains downstream C17.

### Closed-solid viewport mesh validation (C18)

The C17 screenshots exposed an ingress defect in `BucketLink2`: the source BRep
is valid/closed, but `Shape.tessellate(.2)` omitted a planar face. Its exported
mesh had 217 boundary edges; back-face culling exposed the missing wall and
made the curved link look transparent. Both renderer controls consumed the same
incomplete triangles, so their agreement did not detect this CAD-to-mesh error.
The C17 images/receipts are preserved as historical evidence, including the hole.

`freecad_mesh_export.py` now validates triangle indices/coordinates, directed
edge incidence, degeneracies and positive signed volume for closed CAD solids.
A valid ordinary tessellation stays unchanged. Only an invalid result is retried
through `MeshPart.meshFromShape` on fresh geometry copies, retaining .2 linear
deflection and testing .2/.3 angular deflection. An invalid final result refuses
attachment before UI takeover. No source CAD shape is modified/saved and no
back-face or material-opacity workaround is used.

For this link, the .2-radian candidate still has 20 open edges and is rejected;
the .3-radian candidate is closed with 1716 triangles and zero boundary or
inconsistent edges. The closed mesh volume is about 17038.17 versus the CAD
solid's 17079.26 (about .24% difference). The preview independently checks a
one-percent volume bound for this known part. Signed volume on an open candidate
is diagnostic only and must not be interpreted as the CAD volume.

`mesh-reports.json` preserves each candidate and source volume. `mesh_builds`
still counts 13 exported link meshes; `tessellation_attempts` records all 15
attempts explicitly. This is one-time CAD import, before the same single native
capture/updater/planner; scalar edits and interaction never retessellate.
The [C18 receipt](validation/coin-render-freecad-mesh-opacity-20261029/README.md)
contains the corrected native image, original comparison and geometry checks.
The helper has six independent unit cases; the actual native viewport tests
strength/direction/zero-restore, selection/movement of the repaired link,
restoration and static reuse, followed by independent color/depth replay.

### Smooth planar reflection over retained buffers (C19)

C19 connects one ideal planar PBR mirror, adapting the reflection matrix and
auxiliary far-plane fit from pinned `codex/pbr-clean`. The executor derives a
mirrored camera, clipping plane, light vectors and tangent parity from captured
values. It encodes a separate HDR/depth view using the **same VBO/IBO** and
already supplied draw order, skipping coplanar receivers. It does not construct
a child scene, invoke a scene action, copy the frame geometry, call a composition
classifier or build another semantic plan. A separate GPU view is additional
rendering work, not another scene capture.

The target's `CoinRenderPbrEffects` exposes `planarReflection`, an authored unit
`reflectionPlane`, tolerance, `reflectionScale` (1/2/4) and an explicit framebuffer
budget. Smooth isotropic coplanar draws with matching normals receive the image;
rough/normal-mapped receivers, absent receivers, layers/multiple cameras and
exhausted clip slots are refused. The current physical scene remains opaque PBR
triangles on BGFX/Vulkan. The reflected view flips face/tangent handedness,
clips geometry on the far side of the plane, and preserves the main camera's
XY projection. Its far plane can grow to fit the longer reflected ray path;
primary depth never changes.

The auxiliary pass shares the current IBL textures and physical world shadow
map. Main-view SSAO stays enabled, but it is explicitly disabled in the reflected
pass: the main camera depth image cannot provide reflected-view SSAO. This does
not implement rough local reflection filtering, probes, recursive mirrors or
ray tracing. It is the source branch's smooth planar profile, with an execution
path adapted to consume retained buffers directly.

The framebuffer uses RGBA16F + D24S8 (12 bytes per reflected pixel). At 1234×886:

| Scale | Reflection size | Attachment bytes |
| --- | --- | --- |
| 1 | 1234×886 | 13119888 |
| 2 (default) | 617×443 | 3279972 |
| 4 | 309×222 | 823176 |

Admission charges the old and candidate attachments during replacement. GPU
geometry and environment uploads are shared, with no reflection pixel readback
or Qt image transport. A changed view/content conservatively redraws the mirror;
forced static native repaints reuse the committed main HDR and repeat neither
mirror nor main shading. Disabling the effect retires its attachments. These
figures are allocation footprints, not whole-process/device memory or FPS gains.
Reflected draw metadata has a separate 16 MiB logical payload bound.

FreeCAD studio ingress authors a polished planar receiver when the effect is
selected; the floor still enters the same root/capture/plan. The optional `REFL`
packet follows existing effect/material packets; old packets remain valid.
`COIN_ASSEMBLY_REFLECTION_PREVIEW=1` records on/off, quarter/full resolution,
restoration, selection and movement using the same camera and material values.
The studio's floor plane follows the exported CAD bounds. The separate C18 mesh
validation remains active, including the repaired `BucketLink2`.

[Qualification](validation/coin-render-retained-pbr-planar-20261029/README.md)
includes an independent virtual-geometry oracle: a white ideal mirror reproduces
1723 pixels exactly, keeps primary depth unchanged, clips an emissive object
below the plane and performs zero extra composition classifications. It also
checks scaled buffers, offset/tilted-plane math, preserved world light position,
rough/missing receiver refusal, budget/scale errors and retry/publication.
The actual AssemblyExample campaign has nine native GUI stages and 20 exact
retained/callback offscreen color/depth buffers. Material/effect/resolution
changes preserve one capture, one plan and one transport. Combined selection and
placement restoration retains the existing conservative single recapture.

Regression qualification also exposed an uninitialized `SoRenderParams` clear
color in external-plan callers. Candidate F8 now explicitly initializes it to
transparent black alongside identity matrices, with a Coin-only regression. This
small native default fix is separate from C19's executor/application feature.

Remaining PBR work after C20 includes local probes,
stationary refinement, geometry AO, area lights, skybox/dynamic environment,
transparent/glass/mixed composition, wgpu/native GL execution and persistent
FreeCAD material authoring. C19 introduces no new native fork dependency. C20 below adds the bounded rough planar profile.

### Rough planar reflection over retained HDR (C20)

C20 connects the pinned `codex/pbr-clean` 128-tap isotropic GGX filter to C19's
existing reflection image. `roughPlanarReflection` explicitly enables the finite
proxy model; material roughness selects its lobe. The shader is byte-identical to
the pinned source. CPU mapping uses world bounds from captured non-receiver
triangles, a power-of-two canonical scale, the physical plane and the existing
camera/projection. Receiver-slot membership replaces the source's presentation
flags. This does not traverse a scene, capture or classify composition again.
The same auxiliary HDR image, VBO/IBO, draw order, IBL and world shadow map are used.

The actual reflected RGBA is filtered in premultiplied form with linear sampling.
Missing/out-of-view samples keep their coverage weight so the existing environment
fallback fills holes without a black halo or double brightness. Roughness at or
below .045 preserves the C19 point-sampled smooth path. Zero proxy height preserves
the pinned source's unfiltered fallback. The depth proxy is an approximation of
CAD bounds, not ray tracing or a guarantee of geometrically correct rough reflection.

Receivers remain isotropic and planar, with unperturbed normals. Rough-profile
clearcoat and ORM maps are refused: a single authored roughness cannot represent
their separate or varying lobes. Body materials retain their previous anisotropy,
normal maps and clearcoat support. The CPU mapping and 256-byte uniform working
charge join the reflection attachment budget; changing roughness allocates no
additional texture or geometry. Nonuniform/sheared camera transforms and nonfinite
bounds are refused transactionally. Every draw resets the rough selector and
normal/height state to prevent leakage into smooth, physical or other-target draws.

The FreeCAD bridge appends an optional `ROUGH 0|1` packet; existing packets remain
valid. `COIN_ASSEMBLY_ROUGH_REFLECTION_PREVIEW=1` selects a polished metal floor
with roughness .32, with on/off, resolution, selection, movement and static-reuse
controls at a fixed camera. [Qualification and images](validation/coin-render-retained-pbr-rough-planar-20261029/README.md)
record the actual results. This feature stays entirely in CoinRender application,
Core mapping and BGFX execution code; no new native fork semantic dependency.

## Current coverage

The integration is broader than the original million-city profile. It is still
opt-in: it does not replace every existing CoinRender capture/manager entry point.

| Resource or behavior | Coupling status |
| --- | --- |
| Cube, FaceSet, Sphere, Cone, Cylinder | Connected through the native primitive capture. |
| IndexedFaceSet, TriangleStripSet, IndexedTriangleStripSet, QuadMesh | Connected, including indexed coordinate bounds validation. |
| LineSet, IndexedLineSet, PointSet, wide unpatterned strokes | Connected through the same Core stroke expansion used by the current path. |
| Explicit normals, normal binding, material binding | Connected; unpacked per-face and per-vertex material arrays are covered. |
| VertexProperty geometry | Connected for positions/normals/UVs; packed orderedRGBA remains rejected. |
| BASE_COLOR and PHONG | Connected with directional, point and spot lights. |
| SoPBRMaterial | Opaque factors/clearcoat, isotropic IBL, native Xlib presentation, directional shadows/SSAO; C16 adds base/ORM/emissive images, normal/height maps and incremental scalar edits; C17 adds anisotropy/VNDF global IBL and C19/C20 smooth/rough planar reflection on BGFX/Vulkan. |
| Ambient environment, attenuation, HAZE/FOG/SMOKE | Captured once into native IR and mapped to existing shading. |
| Depth provenance and polygon offset | Shared native depth-policy element; authored ignored fields retain their masks. |
| World clipping planes | Up to eight captured at plane traversal and retained in IR. |
| Stored 2D textures | L/LA/RGB/RGBA, MODULATE/REPLACE/DECAL/BLEND, UVs, texture matrices, wrap/filter and shared mip generation. |
| NONE, screen-door, immediate/deferred BLEND/ADD, sorted-object BLEND/ADD and sorted layers | Native captured, opaque-first or back-to-front order remains authoritative; no Core reclassification or sorting. Sorted-triangle modes remain rejected. |
| Sampling, stored sRGB, canonical coverage, MSAA | Transport propagates target options and ordinary target admission remains authoritative. |
| Weighted OIT and depth peeling policies | Resolved native items select existing executor mechanisms; they do not invoke another scene planner. |
| Group/Separator, TransformSeparator, Switch, transforms, cameras, Units, ResetTransform | Admitted exact built-in types. |
| ShadowGroup and bump mapping | Native ShadowGroup and legacy bump semantics remain uncoupled. C14 supplies bounded directional shadows; C16 connects stored normal/height sources for PBR. |
| RTT, cube maps, volumes and combine | Not coupled. C16 separately supports PBR stored image roles on units 0–7 and planar/explicit/default UVs; this does not enable general legacy multitexture/combine. |
| Annotations, background/foreground, depth-clear events and scene layers | Not coupled yet. Transport currently admits the native Main stage. |
| Selection, highlights and picking | Native metadata exists; the application/executor interaction has not been coupled. |
| Packed colors, two-sided lighting, surface wireframe/point modes, stippled strips, bounding-box complexity, Text2/Image and VRML | Still rejected; additional source semantics must be preserved before enabling them. |
| Async submission, multipass, callbacks/manager orchestration and portable user shaders | Existing CoinRender paths remain separate entry points. |

The table records implementation scope, not universal certification of every
state combination or physical adapter. Capability and publication failures
remain explicit. Changing targets with different sampling/color/transparency
options invalidates the owned transport, while preserving the native capture and
plan. Exact node type checks reject arbitrary subclass behavior.

## Coupling contracts added in this expansion

`SoEnvironment::IRRender` and `SoComplexity::IRRender` capture the same logical
element state as the existing reference path. Native IR now retains fog, clip
planes, texture matrices and explicit depth masks. `SoLightElement` retains
attenuation alongside each light matrix when a light is traversed: an environment
encountered later cannot retroactively change an earlier light.

`SoRenderDepthPolicyElement` is implemented in Coin and shared by the native and
current capture actions. The old private CoinRender header is a compatibility
alias; there is one mask implementation and one element stack.

The transport consumes the native plan order. Alpha-test uses the existing
logical capture helper. Resolved item views derive blend/depth/OIT flags from
captured values and requested execution policy, without classifying or sorting
again. Target validation, resource ownership, error handling and publication are
still the ordinary executor contracts. MSAA depth readback uses its existing
explicit opt-in on both comparison targets.

Stroke expansion is mechanical lowering of captured data. It can create several
triangle packets for one native operation while preserving its place in the
native order. Unchanged lowered payloads remain cached. Changed strokes and
per-vertex material payloads currently rebuild their owned transport from the
updated native frame/plan; ordinary triangle geometry retains its existing span
patches and material copy-on-write. A diffuse edit affecting generated vertex
colors conservatively recaptures once, rather than patching only the uniform and
leaving stale colors. These boundaries are covered by explicit counters.

The admitted type keys are prepared once per capture action, avoiding rebuilding
the enlarged type list at every node of a large scene.

## Work remaining to couple the whole current path

- Preserve effect and producer dependencies in the native frame/plan: shadows,
  legacy bump mapping, RTT, volume/cube textures and general multitexture/combine.
  C16 covers stored PBR roles and normal/height sources only.
- Represent original polygon contours, strip provenance, raster nodes, VRML,
  packed colors and authored shader state before expanding those profiles.
- Map native stages/depth barriers to scene layers and connect selection/picking.
- Route the existing manager, asynchronous tickets, multipass and callbacks through
  the same capture/plan authority once their complete semantics are represented.

No fallback silently runs the old capture after a refusal. Finishing these items
requires extending capture data and native plan operations, not merely widening a
node allowlist.

## Retention and incremental ownership

`CoinRenderRetainedRenderer` owns the native action, root reference, draw list,
plan and a persistent CPU transport adapter, rebound across compatible targets.
GPU resources remain owned by each executor target/device generation. A bounded root
sensor collects field/path notifications. Unchanged roots reuse both capture and
plan. `SoRetainedUpdater` classifies changes and commits native invalidation
revisions and affected command IDs. A plan is built only when its native domain
or draw-list generation changes. No second scene traversal or planner is added.

| Change | Native capture | Native plan | Owned transport |
| --- | --- | --- | --- |
| Unchanged | reused | reused | reused |
| Translation, opaque diffuse | command patch | reused | affected model/material slots only |
| Coordinate3 same-layout geometry | retained resource update | rebuilt once | changed positions/indices in existing spans |
| Clear color | reused | reused | clear state only |
| Qualified single-camera projection (height/angle, near/far/focal) | state patch | reused | camera/state slots only; no vertex/index copies |
| Camera pose, multiple cameras, structure, mixed/ambiguous notifications | one rebuild | rebuilt | rebuilt |

Direct FaceSet triangle resources and their recipes come from the fork. A shared
recipe can retain one native geometry resource across many occurrences. A
geometry update uses the fork's resource transaction. The adapter checks layout
and delta continuity, then patches only changed positions/indices in existing
transport spans and consumes the updated native plan. Structural/topology changes
still take one full capture/transport. General GPU geometry subrange uploads are
not implemented; executor packing/upload work remains distinct from transport. Native incremental arena
allocations remain alive until full capture reset; this is not a bounded-memory
claim for arbitrarily long geometry-edit sessions.

The adapter copies native geometry into its own frame on full conversion, then
patches committed state/geometry commands using exact native delta continuity.
Plan changes refresh only the authoritative schedule over existing draw spans.
A missed delta, changed target owner or failed execution invalidates reuse.
An owned immutable frame loan supplies existing wgpu/BGFX validation and packing
caches with a trustworthy revision. Static submissions use REUSE; state updates
use RESOURCE_REBUILD with the previous revision. Existing executor resource
caches and incremental packing remain responsible for GPU work. Raw external
`SoRenderBackend::render(list, plan, ...)` remains untrusted and always converts,
so editing a supplied plan cannot accidentally reuse an old order.

Native object/occurrence IDs are still not used as baked-geometry cache keys:
those identities do not prove material-slot equivalence. Equal complete material values now share slots; diffuse updates split shared
slots before changing them, reuse unique slots and recycle vacancies. This
reduced the measured wgpu diffuse cache bytes 1,024-fold. BGFX also uses its
existing unlit instanced shader and uploads only changed instance ranges after
an owned mesh/index equality check. CPU frame reuse still does not imply a
minimal GPU geometry upload.
Target transactional publication preserves the previous image and serial on
unsupported/invalid updates. No native storage or schedule loan escapes the
synchronous execution scope. Callers must leave scene notifications enabled,
render on one thread, and avoid mutating the scene during render.

## Source and ABI

Imported foundation: FreeCAD/coin commit
`17a9e4b21b643cb44d42e103183d8384eef6a6a5`, PR #80, branch
`stack/retained-instance-data`; it is not merged freecad-master functionality.
See `cmake/patches/freecad-retained-origin.txt` for adaptation boundaries.

The new IR virtual methods in SoNode/SoShape change core ABI. Rebuild Coin,
CoinRender and all consumers together. This branch does not establish Coin 4
binary compatibility; the pre-existing SoBumpMapElement::source concern remains.
No FreeCAD or Quarter application integration is included.

The pinned Rust dependency overrides remain at
`b39b068fe5aed4782790075866bee8d010bed3ec`; no dependency correction was removed.

## Validation

Configure separate builds with COIN_BUILD_RENDER=ON, COIN_BUILD_TESTS=ON and
COIN_RENDER_BACKEND=RECORDING, RUST_BRIDGE or BGFX. Existing dependency/toolchain
requirements in coin-render-build.md still apply. Build and run:

```
cmake --build <build> --target CoinRenderRetainedConnectorTest CoinRenderRetainedIncrementalTest CoinRenderRetainedBenchmark
ctest --test-dir <build> -R '^CoinRenderRetained' --output-on-failure
```

The connector test checks capture/plan counts, zero invocations of the old
composition classifier (test-only counter), coplanar draw order, normalized
depth against an analytic control, externally reversed order, malformed-plan
rejection, unsupported node/material rejection, preserved pixels/serial and
recovery. It is the same test source for all executors.

The reference test creates its own desktop OpenGL EGL pbuffer on supported
builds, runs Coin's current SoGLRenderAction renderer and compares flat RGB
interiors. It excludes a fixed one-pixel boundary and permits two channel levels
of rounding difference; more than 3000 pixels must be compared with no remaining
mismatch. It does not establish cross-device raster-edge identity.

Linux results on 2026-10-27: all four retained tests passed in RECORDING,
wgpu/Vulkan and BGFX/Vulkan. Incremental coverage compares exact RGBA and depth
against forced native rebuilds for static frames, local and inherited transforms,
diffuse and geometry edits, clear, camera, structure, mixed changes, ambiguous
shared state, invalid patches and recovery. Repeated auditor snapshots are also
checked. Four selected existing RECORDING regressions passed: CoinRenderActionTest,
CoinRenderBackendBoundaryTest, CoinRenderTransformCoreTest and CoinRenderCompositionTest.
The existing sensor scheduling and path auditor suites also pass (see benchmark report).

The GPU tests used the AMD Vulkan ICD. EGL OpenGL controls reported llvmpipe for
RECORDING and NVIDIA RTX 3060 Laptop for GPU test runs; these are not same-device
raster-edge comparisons. These are bounded local results, not Windows,
other-device, presentation or complete feature qualification.

See [current measurements and limitations](coin-render-retained-benchmark-v2.md) for the
current/retained/forced-rebuild comparison and reproducible commands.

Android x86_64 AVD results for this working tree are recorded separately in
[Android retained validation](coin-render-retained-android.md): wgpu/GLES only,
with Vulkan/BGFX limitations and no physical-device or window qualification.

## Android million-city window integration

The Android viewer now selects this connector with `coinrender_retained=true`
(the current path remains the default). Shared overall-material SoCube nodes
publish one native geometry resource per node revision. The transport interns
geometry by native resource and material, and equal camera snapshots. Normals,
PHONG material and directional lights are mapped to the existing executor.
The fork direction points toward the light; the executor direction points from
it, so the connector negates it and preserves intensity-weighted color.

A compatible window/offscreen target rebind preserves capture, plan and owned
CPU transport; existing target/device cache validation still applies. Static
execution caches the identity schedule predicate instead of scanning all
commands to rediscover it. Geometry and material updates preserve copy-on-write
ownership when multiple commands share transport spans.

See [the Android million comparison](coin-render-million-retained-android.md)
for full-scene equivalence, timing, memory, and the remaining regressions.

## Compact native command storage (v2 history)

The following describes v2; direct compact capture below supersedes its storage path.

The private retained facade opts into exact command-template interning after a
successful publication. A compact entry owns the occurrence model matrix and a
32-bit template index (68 bytes on the measured Android x86_64 ABI). Templates
retain all other command members, including camera matrices, material, geometry
handles/descriptors, picking ranges and producer identity. Keys serialize members
explicitly, excluding structure padding and container representations; equality is
exact and geometry/pixel pointer lifetime contracts are unchanged. No new scene
traversal, sorting, capture or planner is introduced.

Native planner, updater and connector reads use value snapshots. Supported
per-command updates replace a compact entry and intern changed common state,
without expanding the complete list. Append/truncate/checkpoint rollback also
preserve compact storage. Existing reference and contiguous-pointer APIs lazily
materialize an ordinary owned command array, preserving their behavior. The next
private render may compact it again. Consumers must rebuild after the private
list layout changes; legacy command references must not cross render calls.

Lists below 64 commands or with too many distinct templates retain the ordinary
representation. Template history is bounded by a 4096-entry collection threshold;
failed/unprofitable compaction is not retried on an unchanged content revision.
Allocation failure in this optional optimization does not invalidate a successfully
published target. An already compact list stays compact during static execution.

The shared Cube recipe now publishes 24 indexed vertices and 36 indices, retaining
face normals, UVs and winding. It maps each original triangle to the same face
range. The executor therefore receives the same vertex workload as the current
capture. The adapter also reserves its draw/state/schedule upper bound from the
supplied plan once, avoiding geometric vector over-allocation.

The controlled Android follow-up is documented in
[the second million-city comparison](coin-render-million-retained-android-v2.md).

## Direct compact capture (v3 history)

The facade now opts into compact command emission before traversal. `clear()`
starts another compact frame while preserving that preference. Producers intern
common state while appending commands, so the ordinary complete-command vector
and a subsequent full conversion are avoided. Existing reference/pointer APIs
still materialize the owned array; diverse streams fall back to it after the
4096-template bound. The dense-to-compact pass counter is exposed diagnostically.

Occurrence entries now hold six exact matrix coefficients (diagonal and
translation) plus the template index: 28 bytes on the measured ABI. The other ten
coefficients, including shear/rotation/projective terms, are included in the exact
template key. Reassembly copies values, without deriving or approximating them.
Template-key serialization uses a fixed member buffer before constructing the
lookup string, avoiding a string append call for each scalar member.

Native path ownership and branch dependency lookup share one flat node index.
Its node pointers and dependency heads are stored in separate arrays. Ownership
still refs each distinct node once, with transactional replay deletion and probe
cluster repair. Consecutive commands under a branch share an interval; full path
and child-index filtering still disambiguate instanced branches during updates.
Path offsets and dependency indices use checked uint32 storage, authored
visibility is bit-packed, and excess vector capacity is trimmed once per capture
generation. Overflow rejects the frame explicitly instead of truncating indices.

These operations change storage only. The fork remains the sole scene capture,
incremental updater and planning authority. Mechanical capacity trimming can
still copy arrays; zero conversion passes does not mean zero allocator traffic.
See [the third Android comparison](coin-render-million-retained-android-v3.md)
for the measured improvements, preserved results and BGFX diagnostic limitation.

## Compact resolved schedule (v4)

The adapter encodes the native plan as implicit identity in the already ordered
owned draw array, or a uint32 permutation when needed. Schedule views derive
range/depth fields from that immutable transport. Neither representation invokes
the common classifier/sorter. Target preflight matches frame/revision/policy and
preserves lexical authority; ordinary vector receipts retain reference identity.
Legacy consumers can expand the resolved order temporarily. Wgpu/BGFX consume
compact item views directly. The million's persistent adapter schedule is zero
bytes, removing its former ~53.4 MiB item array.

Ordinary native node ownership now resides in the flat path/dependency index;
only speculative additions need a rollback list. Path lengths derive from
adjacent offsets. The original combined BGFX regression is covered without
resetting the common transform: known authored-depth mask bits no longer force
a fallback when resolved opaque depth state already qualifies for instancing.
Unknown masks and unsupported effective depth still decline. See
[the v4 comparison](coin-render-million-retained-android-v4.md) for exact-depth
checks and the Android measurement where the memory excess is eliminated.


## Integration names and ownership

The public entry point remains `CoinRenderRetainedRenderer`. Private integration
names describe their responsibilities:

| Component | Responsibility |
| --- | --- |
| `CoinRenderSceneCapture` | Validate supported scene semantics during the native capture traversal. |
| `CoinRenderSceneChanges` | Collect field/path notifications; flag changes requiring a fresh capture. |
| `SoRetainedUpdater` / `SoRenderPlanner` | Apply native incremental updates and build the authoritative native plan. |
| `CoinRenderRetainedTransport` | Convert or patch owned executor input using the supplied capture and plan. |
| wgpu / BGFX | Pack, upload and execute that input for the target. |

`CoinRenderRetainedTransport` was previously called
`CoinRenderRetainedBackend` (and identified itself as a connector). It implements
the native `SoRenderBackend` interface as an adapter; it does not introduce a
GPU backend or another planner. Its `TransportCache::plannedDrawIndices` is a
permutation of the supplied plan, with empty storage denoting identity.
Content, plan and resource revisions now have explicit names.

The private `submitOwnedCapture` entry point selects
`CaptureOwnership::RendererOwned`. Only the renderer can call it. Public native
`render` selects `External`: every call validates and converts the supplied
input, including a plan edited in place without changing any draw-list revision.
This replaces an ambiguous boolean authority argument with an explicit ownership
contract. Native fork names and the public renderer API are unchanged.

Archived benchmark receipts retain their original names and source hashes.
This refactor does not establish new memory or performance measurements; the v4
comparison above describes the previously measured implementation.


Naming-refactor validation: 6 recording, 6 wgpu and 7 BGFX tests passed,
including composition, exact-depth retained-city equivalence, incremental
updates, external in-place plan edits, borrowing disabled and GL reference.
BGFX also passed instancing. Logs are in
[the validation directory](validation/coin-render-retained-naming-20261027/).
No new Android benchmark was run for this naming refactor.


## Broader coupling validation (2026-10-27)

The feature campaign compares the selected executor against the existing capture
with the same scene, camera and target options. It covers 64 visible fixtures,
static reuse, incremental translations, transparent order changes, per-vertex
color edits and rejection of invalid coordinate indices. RGB permits at most 48
edge pixels with channel error above 4 (192 for stroke cases); depth permits at
most the same edge budget above 3e-6. Both paths must produce visible pixels.
This is not a byte-identical claim for every feature. The city regression keeps
its separate exact BGFX depth check and single-capture/plan/transport assertions.

Policy fixtures separately cover MSAA4 (depth readback explicitly enabled),
portable sampling, canonical coverage, stored sRGB, weighted OIT, peeling and
seven additional native-order transparency policies.
Android uses the same explicit GLES renderer as earlier qualification. Its device
reports no anisotropy, so the feature comparison explicitly sets the same limit
of one for both paths; production admission still refuses unsupported anisotropy.

Final logs and commands are saved under
[the broader validation directory](validation/coin-render-retained-coupling-20261027/).
The previous million-city memory/performance receipts remain historical. A separate controlled Android campaign records the million-city measurements
for this expansion; those results are not inferred from the small fixtures.


World/view-space invalidation is tracked by native capture paths for cameras,
positional lights and active clipping planes. A translation preceding one of
those state sources conservatively recaptures once; translations after them
retain ordinary incremental updates. This prevents a matrix-only patch from
leaving old light positions, view state or clipping equations in the transport.


Primitive assembly also preserves one object bounding center across all material
batches of a shape. The center is computed while copying captured vertices;
splitting a transparent object into face materials does not turn it into a set
of independently sorted faces. Per-face transparency is compared in the regular
campaign and in every execution/transparency policy fixture.


Stable one-child switches whose child does not leak state now update native
visibility with the correct applied-root path offset. A changed visible draw set
rebuilds transport from that updated native capture/plan, while unchanged hidden
commands are skipped. Switches that can leak inherited state recapture. Matrix
or material replays that cannot reach a hidden tail also recapture instead of
publishing default/stale state.


Single-camera projection changes replay only the notified camera path. They are
admitted only when viewport and view matrix remain identical and all native
commands refer to that same camera revision. Native templates are re-keyed
transactionally; ordinary small command lists update their camera fields in
place. The transport patches camera/render-state slots and automatic fog distance
without copying vertices or indices. Both ordinary and compact storage are tested
against full reference capture, including explicit fog visibility equal to the
previous far plane. Camera poses, ambiguous scopes and multiple cameras retain
conservative recapture.

Texture capture remembers the upload quality of each retained image revision,
matching the current path when global texture quality later changes or disables
texturing. Pixel conversion and mip generation reuse the existing ImageCore.
Pending filename loads, compressed upload and legacy power-of-two override
policies are refused until their image ownership/resize semantics are represented.

Final qualification: 8 recording tests, 21 wgpu tests, 23 BGFX tests (including
Core/instancing), plus 6 Android GLES runs. The feature test additionally exercises
switch hide/show, hidden material edits, state-leaking switches, texture quality
transitions and projection updates in normal and compact command storage.


The fresh [million-city Android comparison](coin-render-retained-coupling-android.md)
records six processes, identical color/depth, first-call gains and memory with
swap reported separately. Fourteen additional existing-path controls pass; the
projective mip legacy CoinGL oracle remains a documented failing control.
