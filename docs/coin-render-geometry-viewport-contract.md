# Geometry and viewport profiles (P02/P04/P05/P06)

The 2026-10-06 campaign and portable extension qualify the explicit cells below;
they do not certify every shape × state × platform × driver combination.

## External viewport

Coin rectangles retain lower-left origin and positive dimensions. Intersect them
with the target to limit executor viewport/scissor without changing original
projection. `CoinRenderTransformCore::clippedViewportTransform` compensates APIs
requiring an internal viewport. For original `(x,w)` and intersection `(c,d)`,
NDC uses `scale=w/d`, `translation=(2*(x-c)+w)/d-1`; Y is analogous and Z/W stay
unchanged. BGFX maps to full target with common scissor. wgpu applies this Core
transform in normal packing, early/late/incremental batching, instancing and
camera patches. Coin depth `[-1,1]→[0,1]` conversion happens once; light/normal/
fog/texture spaces, snapshots and original plans stay unchanged.

Empty intersection is valid: retain clear, emit no geometry or annotation-depth
clear. Historical private Rust revision **46** preserved layouts and encoded empty
viewport as `[0,0,0,1]`; legacy `[0,0,0,0]` still means full target. Raw external
rectangles remain invalid at the bridge: Core must convert them. Each RTT producer
uses its own extent; admission of an external viewport does not qualify every
RTT format/transparency/shadow combination.

## Bindings and captured geometry

IndexedFaceSet distinguishes indexed/nonindexed normal bindings. PER_FACE_INDEXED/
PER_PART_INDEXED select the indexed normal; PER_FACE/PER_PART/PER_VERTEX ignore
normalIndex, with PER_VERTEX following emitted occurrences even when coordinate
order differs. Missing/out-of-range indexed normals cause atomic fallback to
native generation; crease-angle generation retains its explicit indexed route.
Materials/alpha/texture indices are unaffected. `CoinRenderIndexedGeometryCoreTest`
checks independent expected vectors, reversed order, applied/ignored indices and
fallback without partial geometry.

The initial `CoinRenderGeometryViewportTest` has **214 scenes/backend**: **118**
rectangles/resize/styles, **80** FaceSet/IndexedFaceSet binding/normal/alpha cases,
and **16** light/fog/range/viewport cases. All use fast paths on/off as applicable.
GPU scenes compare CPU/GPU/CoinGL. Face interiors include uncovered pixels to
detect missing/displaced geometry, excluding one raster-edge pixel. Strokes need
at least four common covered samples. RGB bound is four levels, without relaxed
tolerances after failures; empty/INVISIBLE requires no color anywhere. A state
fixture installs viewport before camera because native offscreen rendering
otherwise repositions it. Annotation controls test internal/external/empty depth
barriers and rollback. FFI numerically reconstructs original centers after batch/
resize; Rust tests legacy full/empty/invalid viewports.

## Portable extension

The extension preserves the five-binding FaceSet/IndexedFaceSet matrix and adds
ten exact types: Cube, Sphere, Cone, Cylinder, QuadMesh, TriangleStripSet,
IndexedTriangleStripSet, LineSet, IndexedLineSet, PointSet. It checks **seven paired**
material/normal bindings, supplied/generated normals, heterogeneous alpha and fast
paths. Styles also include IndexedFaceSet. The **49** mixed binding pairs are
not all qualified. Classic PHONG remains Gouraud, with asymmetric directional/
point/spot lights, NONE/HAZE/FOG/SMOKE and internal/external viewports. Lines/points
without supplied normals use BASE_COLOR like native nodes; supplied endpoint
normals are lit before clipping/expansion. QuadMesh/strips with face bindings
retain original provoking-vertex flat primary color.

LINES/POINTS binding/alpha/external-viewport junctions use a portable oracle;
independent binding/light matrices retain CoinGL. DEFAULT/Plane/custom function,
unit zero/multitexture, clipping and alpha are checked on faces and
Cube/Sphere/Cone/Cylinder in the stated cells. Each finite depth-range endpoint
is clamped independently to `[0,1]`, admitting reversed/collapsed ranges; NaN/Inf
reject before publication and corrected input recovers. No geometric depth clamp
is added. Historical revisions **48**, later RTT **49**, preserve layouts.

`CoinRenderDepthCore` resolves slope/maximum from original triangular faces in
window space. Reversed ranges use gradient magnitude; executor format supplies
units quantum. Strokes retain the original face slope/maximum. CPU uses **1/256
pixel** window coordinates to stabilize coincident-strip ownership. Collapsed
contours/boxes preserve authored endpoint direction after clipping. Common strips
shift one subpixel up/left for top/left edge ownership; boxes retain native profile.
Coplanar strokes with active test/write may use an equivalent collapsed range
without changing original Z clipping. This is qualified for campaign APIs/GPUs,
not every physical rasterizer. Cached indexed strokes recompute payload digest
after expansion, including local indices/positions/attributes; static reuse remains,
lighting/fog/viewport/clipping changes invalidate it.

Separate fixtures cover external multiregion composition, annotations, cropped
shadows and RTT capture/FBO/pbuffer/staged/direct. Shadow controls require visible
on/off difference; crop expectations come from a larger frame with identical camera.
Native fixes also correct QuadMesh PER_FACE advancement, TriangleStripSet's first
face binding and IndexedLineSet's implicit line/segment occurrence indexing;
independent slots/pixels verify them.

## Remaining differences and evidence

Procedural probes remain opt-in failing reproducers while their differences exist:

```sh
CoinRenderProceduralTextureTest --probe-sphere
CoinRenderProceduralTextureTest --probe-cone
```

Original NVIDIA sphere LINES/DEFAULT maximum was **5** vs CoinGL (CPU/GPU **1**);
cone LINES/DEFAULT CPU/GPU MAE **2.94**, maximum **120** at coincident side/cap
contours. The later sphere native reproducer still measures maximum **9** while
CPU/GPU is **1**. These do not alone identify the incorrect rasterizer. Curved/
coincident native junctions and transparent shared endpoints remain studies,
with strict references retained. Neither every mixed binding pair, extra shapes/
subclasses, flat-with-shadows/fragment-lighting, nondefault
`COIN_QUADMESH_PRECISE_LIGHTING`, extra RTT formats, other drivers nor Windows/
host integration is qualified here. Raw private reversed-range/offset plans must
be Core-resolved first.

Historical final campaign: **204 wgpu CTests of 206**, **270 BGFX of 272**, with
the same two known skips per build; **40 Rust** passes; RECORDING ten passes/two
GPU skips after CPU controls. Each GPU geometry run passed **1290 scenes**;
procedural **311 scenes and 69 CoinGL references** per qualified route. OpenGL's
**84** curved cells passed after common ownership policy, while strict native
sphere differences stayed open. Initial fog/shadow timeouts passed isolated
under original deadlines and source/tolerances. These are historical receipts,
not new HEAD tests; complete logs remain in the [archive](coin-render-evidence.md).
