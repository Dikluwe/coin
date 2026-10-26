# SoDrawStyle contract

P02/F04 remains open for the complete matrix. INVISIBLE is common; LINES/POINTS
are resolved in Core for CPU/BGFX/wgpu. C04.1 extends concave contours,
per-occurrence attributes and nonplanar offsets in its qualified cells.

`SoDrawStyle.cpp` respects ignored fields and independent overrides for style,
width, size and pattern. CoinGL uses glPolygonMode for FILLED/LINES/POINTS;
`SoShape::shouldGLRender` handles INVISIBLE through SoShapeStyleElement, including
native lines/points. Coin primitive callbacks split QUADS/QUAD_STRIP into triangles;
converting each triangle into three lines would expose internal diagonals.
Recover original face details or verified procedural assembly instead. Clipping
must mark new boundaries, preserve surviving edge flags, cull correctly and
interpolate attributes; lighting precedes clipping.

## Invisible shapes and contour capture

Wiring's `CoinRenderFramePlanBuilder::isShapeInvisible()` reads effective Coin
state rather than reinterpreting fields. It prunes only shapes before texture
unit validation/indexed preparation. Since Coin continues other pre-callbacks
after PRUNE, indexed callbacks and direct builder/primitive entry points also
consult that guard. Groups/properties/siblings still traverse. Invisible shapes
add no geometry/material/render state and do not validate GPU resources used only
by them. State nodes with their own callbacks, including combine/RTT, retain
existing validation/execution. All-invisible frames publish clear; style changes
invalidate normal Coin caches. This change needs no GPU executor/ABI extension.

Capture uses effective style and native callback materials/normals/UV. Recoverable
SoFaceDetail preserves contour order, detail identity, coordinates and attribute
consistency. IndexedFaceSet style paths use callbacks for details. Exact
Cube/Sphere/Cylinder/Cone recover known QUAD/strip/cap assembly from SoGenerate;
verify callback pairs. Subclasses do not automatically inherit assumptions about
custom primitive generation. Compatible derived generation/recoverable details
are admitted only after validation; absent/incomplete identity or contradictory
attributes returns UNSUPPORTED, never triangulation-as-contour approximation.

`CoinRenderPolygonStyleCore.h` receives contours/snapshots and resolves Gouraud,
homogeneous six-plane view clipping, user planes, culling, topology, indices and
state. Whole-contour clipping creates cut edges/endpoints without exposing quad
diagonals. Culling accounts for projection/winding/mirroring. Edge-on projected
faces still emit edges/points with culling off. Do not deduplicate occurrences
across polygons: multiplicity affects alpha/depth. `CoinRenderLightingCore.h`
shares the Gouraud calculation with CPU. Newly clipped vertices interpolate lit,
clamped primary color, alpha and UV. Infra consumes resolved geometry without
reinterpreting SoDrawStyle. Native line/point topology survives style changes.
Screen-space strokes do not qualify ordinary camera/material reuse.

## Original-face depth bias

For planar original faces Core computes window slope
`m=max(abs(dz_window/dx_pixel),abs(dz_window/dy_pixel))`, including Coin depth
range, and stores `factor*m` in `polygonOffsetSlopeBias`, zeroing native factor.
Clipping/culling precede expansion; bias changes neither vertices nor clipping.
Planarity is checked on the original contour even if clipping hides part, with
object-space extent tolerance **1e-5**. Stroke-triangle slope cannot replace
original-face slope. Required zero projected area with active factor is unsupported.

BGFX adds resolved slope to its constant shader term, applying range/bias/clamp
in fragment depth. wgpu selects `fs_depth_bias`, uses viewport `[0,1]`, native
factor/units zero and shader range/bias/clamp; units use D32Float quantum
`2^(exponent(maxDepth)-23)`, retaining fractions. Infra owns format precision.
Other draws retain their native pipeline. This avoids premature viewport clamp
to Coin's range before bias. CPU resolves bias after range for its admitted profile.
C04.1 nonplanar LINES uses each edge's source-triangle slope; POINTS preserves
incident-triangle occurrences so depth determines the winner for either factor
sign. Without source triangles/required projected area, reject explicitly rather
than invent a diagonal. Maximum original/clipped-face depth is captured before
pattern masking, so eliminating deepest coverage cannot change units quantum.

Historical transport stages: private revision **23**, state **1092 bytes**, slope
float at **1088**; revision **25**, vertex **44**, state **1096**, draw **56**,
`polygon_offset_max_depth_bits` at **1092** (zero absent, otherwise IEEE bits+1).
`screen_space_w` and `fog_eye_depth_plus_one` preserve stroke W and fog (zero
means legacy geometry; explicit distance stores distance+1). Perspective position
is multiplied by W; color/UV interpolate perspective and fog follows texturing
per fragment. Multitexture revision **26** later used vertex **100**, state **2280**;
projective UV and subsequent revisions extend these again. Rebuild current paired
C++/Rust declarations; none of these private revisions versions stable Coin ABI.

## Common stroke raster and line stipple

`CoinRenderStrokeCore.h` owns clipping, width/size, interpolated attributes,
homogeneous weight and fog distance. Textured indexed lines use native callbacks
to preserve UV; solid untextured direct paths remain. Expansion covers whole raster
cells at endpoints to prevent loss on excluded triangle edges. Split at original
endpoints: interior keeps homogeneous interpolation, tiny extensions keep endpoint
W/UV/material/alpha/fog/depth. Internal quad edges cover once; alpha gates detect
double blending.

`CoinRenderLineStippleCore.h` counts unit fragments with diamond-exit rules,
least-significant bit first, repetition **1..256**, continuous phase along a
contour and reset per original polygon. Expansion emits visible intervals only;
width replicates the bit on the minor axis. Maximum **65,536 cells/segment**;
excess returns diagnostic. Measured Mesa assembly starts at closing edge for
TRIANGLES/QUADS and first supplied edge for POLYGON; the connection to provoking
vertex convention is an inference from source/reference, not universal driver law.
Clipped contours use deterministic phase from visible contour. GL permits
indeterminate initial phase for clipped lines. Native comparison therefore
requires one per-edge phase shift explaining all masks/repetitions/widths/modes;
solid/empty preserved/user-cut edges compare directly and unclipped exact phase
remains mandatory. Newly created cut edges need not have identical native phase.

Measured Mesa omitted some near/far cut boundaries that common CPU/GPU retained
under the specified clipping boundary rule. The cause remains undiagnosed;
difference counts are not native pixel-parity passes. Mandatory reference failure
is never turned into SKIP. Near/far controls use LEQUAL at the far edge. Polygon
clipping controls are single-face scenes, not arbitrary overlapping-object order
qualification.

Native LineSet/IndexedLineSet phase continues per polyline/node, across material
packets, repeated vertices and shared endpoints. Reset per segment for PER_PART/
PER_PART_INDEXED material or segment normals when PHONG uses supplied normals.
Segment normals do not change assembly under BASE_COLOR/unsupplied normals.
Wiring captures SoLineDetail boundaries/bindings; transient strip identity lets
Core preserve phase then is removed before submission. Never infer connectivity
from coordinate equality. Patterned IndexedLineSet direct pairs lack strip
boundaries and fall back to callbacks; solid lines keep direct paths. Custom
primitives without native identity remain independent segments, unqualified for
extended continuity. Clipping counts visible fragments; all-driver phase parity
is not established.

Point style on independent line segments emits both endpoints; strips emit shared
points once per occurrence. Portable CPU raster uses **1/256 pixel** and recalculates
post-expansion digest from positions/attributes/fog, not stale indexed identity.
Common strokes move one subpixel top/left in their stated ownership profile;
boxes retain their native special case. See [geometry/viewport](coin-render-geometry-viewport-contract.md).

## Per-occurrence concavity and alpha

A contour vertex is an occurrence, not merely XYZ. Preserve material, normal and
all eight homogeneous STRQ sets. Compact coincident consecutive corners only if
all rasterized attributes and edge flags match. Zero-length edges cannot erase
incoming/outgoing attribute discontinuities. Concave clipping may split components:
use clipped original triangles, cancel internal edges and reconstruct separate
closed contours without artificial bridges.

`--canonical-raster` and `--portable-sampling` are independent public test choices,
forwarded to CPU/fresh targets and checked through effective options. Independent
geometry/masks/texels verify components, attributes/bindings/subclasses/mutation
and transactional rejection. Strict CoinGL comparisons remain: AMD/NVIDIA may
choose different sides of the same edge; native cache still discards differing
attributes at consecutive coincident occurrences (**B17**), so those failures are
not parity passes. **B16** corrected FaceSet OVERALL-normal cache submission so
native reference no longer depends on a preceding draw's normal.

C05.1 qualifies FaceSet BLEND/SORTED_OBJECT_BLEND/SCREEN_DOOR in declared cells.
On split clipping, clear abandoned connected output before reconstruction; POINTS
deduplicates only newly indexed output. Retaining the old ring lost original
corners (**B31**). Alpha/UV preserve outgoing occurrence edges; clip intersections
interpolate endpoint alpha. Independent analytic BLEND controls include FILLED,
clipped edges/points and complete CPU/GPU depth/A/B/A. Immediate/deferred blend
can share RGBA but differ in depth: cross-policy comparison distinguishes channels,
while within-policy recovery demands exact RGBA/depth.

## Evidence and remaining scope

`CoinRenderDrawStyleTest` covers invisible shapes/scopes/ignored fields/overrides,
clear/serial/recovery, recoverable contours/procedural assembly, missing details,
no diagonals, culling/mirroring, near/user clipping, lit UV/alpha/fog/W, projected
slope and fractional units. Numeric controls vary viewport and W **1..4**;
pattern matrices include widths **1/3/6**, repetition **1/2/5/256**, masks
**0/ffff/000f/aaaa/9249**, orthographic near/far and multiple user cuts. Native
line matrices cover both types, bindings/normals, instances and packet continuity.
The original mandatory native reference was Mesa/llvmpipe **GL 4.5 Compatibility,
Mesa 25.2.8**, GLX pixmap/direct context, four-level RGB sample tolerance:

```sh
__GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
xvfb-run -a <build>/bin/CoinRenderDrawStyleTest
```

Select BGFX renderer explicitly with COIN_BGFX_RENDERER. Original repeated closures
passed **19/19 wgpu**, **28/28 BGFX**, **11/11 Rust/WGSL** in the final stipple
profiles; earlier offset stages had **26** BGFX tests. Later C05.1 passed **28/28**
GPU processes/eight CPU controls while preserving **447** common comparisons/cell
and unchanged strict-native failures. Archived logs include intermittent OpenGL
segfault/SIGPIPE/device-loss-setup failures later passing in fresh Xvfb; undiagnosed
intermittency is not stability certification.

P02/F04 remains partial. Broader shapes/subclasses/custom GLRender, mixed bindings,
curved/coincident native junctions, all transparency/texture/offset intersections,
MSAA/other drivers and host applications require their own qualification. Basic
LineSet/PointSet support is distinct from polygon styles. The [archive](coin-render-evidence.md)
retains original staged reports and the detailed historical matrix; no claim here
retroactively converts their failures or limits to passes.
