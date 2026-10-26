# Complexity BOUNDING_BOX contract

The common adapter calls `SoShape.computeBBox` in the effective occurrence state
and captures six local-box quads. Placement uses midpoint of min/max; the weighted
`computeBBox` center still serves transparent sorting. Object transform remains
in draw state. FILLED uses **24 vertices, 36 indices/12 triangles** per occurrence,
canonical normals/UV, material zero, CCW, no culling and one-sided lighting like
`GLRenderBoundingBox`; original bindings/normals do not replace these attributes.
Existing shared plans feed BGFX, wgpu and CPU. This historical change preserved
shaders, FFI/layout and private wgpu revision **45**. OBJECT_SPACE/SCREEN_SPACE
retain normal geometry generation. Coin elements interpret Separators, overrides
and ignored fields; fast paths do not replace boxes or preempt later user callbacks.

## Styles, alpha and callbacks

FILLED/LINES/POINTS/INVISIBLE preserve their semantics. Lines/points derive from
six contours; shared edges/corners retain per-face multiplicity. Finite degenerate
boxes also emit lines/points; only box capture requests this Core preservation.
In the qualified NVIDIA POINTS profile, internal quad triangulation retains
outer-edge flags and clipping may add a point where the internal diagonal is cut.
The transparent gate clips exactly two corners and confirms **100** native pixels,
including that point and A/B/A restoration. LINES emits no diagonals. Diagonal
choice is driver-qualified, not universal GL behavior.

Box diffuse RGBA uses Coin `SoMaterialBundle.sendFirst`/`glColor4ub` precision
before lighting/transparency. Ambient/specular/emission retain original precision.
SCREEN_DOOR retains classification/stipple alpha and sends opaque primary alpha.
Native defer decisions occur before the box. Capture preserves classification of
material arrays and stored images, even when MarkerSet disables texture, through
the shared marker/RTT policy helper. The box snapshot changes
SORTED_OBJECT_SORTED_TRIANGLE_ADD/BLEND to SORTED_OBJECT_ADD/BLEND, preserving face
order as native box selection precedes triangle sorting. CPU interpolates depth
from a reference vertex to preserve constant planes and LEQUAL on coincident faces.

Capture follows every user pre-callback and respects PRUNE/ABORT. Primitive
observers still receive original primitives with their capture suspended; box
geometry stays in the plan. Pre/post callbacks remain observable. VertexProperty
is applied before boxes on shapes whose CoinGL path does so. PointSet reads its
vertices in computeBBox but keeps inherited material. MarkerSet applies its
property, disables texture and uses BASE_COLOR; IndexedMarkerSet retains inherited
lighting/texture at that point. Text2/Image choose boxes before raster preparation,
after callbacks. Empty images and native exits before shouldGLRender emit nothing.
Subclasses with compatible computeBBox are admitted; custom GLRender overrides
that bypass shouldGLRender require explicit adaptation.

## Texture, domain and shadows

Unit-zero DEFAULT/EXPLICIT use box UV, ignoring shape arrays. Apply the entire
matrix and PROJECTIVE/DIRECT_ST policy. Active FUNCTION returns `UNSUPPORTED`:
Plane/Object/Environment can keep GL texgen active, whereas CPU-only functions
are ignored by native cubes. Additional active units are rejected because native
boxes send only UV zero and depend on persistent GL attributes elsewhere.
MarkerSet may ignore these textures within its native texture-disabled scope.
Existing quality/filter/format/RTT/alpha/budget limits remain.

Nonfinite bounds/center/extent or diffuse RGBA outside `[0,1]` return
`INVALID_SCENE` before publication. Empty boxes allocate no geometry, regularizing
native boxes without valid bounds. Failures preserve pixels/serial; a corrected
scene recovers. Nonzero slope factor with zero projected area is `UNSUPPORTED`.

For SHADOWMAP, shouldGLRender returns before BOUNDING_BOX: main draws use boxes,
map capture retains original shapes. Groups with Complexity/inherited boxes/custom
callbacks capture maps separately. The fixture checks **12** main triangles and
**390** original Sphere caster triangles with radius mutation/restoration. Raster
text/image/marker boxes in map groups depend on original raster map capture and
remain outside this historical box profile. Its real spot-map control uses zero
intensity/emissive material to isolate geometry/UV.

[CoinRenderBoundingBoxTest](../testsuite/coinrender/CoinRenderBoundingBoxTest.cpp)
has `--capture`/`--gpu`; GPU requires real GPU/CoinGL. Active-region RGB MAE ≤1,
outliers above three levels ≤max(4,2% of region); general framebuffer alpha is not
qualified. Original Linux/NVIDIA wgpu/Vulkan and BGFX/Vulkan/OpenGL each passed
**137** native comparisons (**411** total), plus two captures. FILLED witnesses
avoid coverage ambiguity; LINES/POINTS use pixel centers for endpoints. NaN/Inf/
extent overflow, rollback and recovery are covered. Original pilots, including
corrected CPU coplanar-face error, remain in the [archive](coin-render-evidence.md).
Other drivers/Windows and performance need their own campaigns.
