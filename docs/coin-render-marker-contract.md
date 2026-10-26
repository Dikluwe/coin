# MarkerSet and IndexedMarkerSet contract

Exact native `SoMarkerSet`/`SoIndexedMarkerSet` types capture bitmaps in common
code instead of inherited point primitives. BGFX/wgpu consume the same screen-space
triangles; this change preserved executor/FFI layouts. A private bridge built only
with `COIN_BUILD_RENDER` supplies registry dimensions/alignment/bytes, copied before
geometry assembly. Public class API/layout is unchanged; CoinRender requires Coin
from the same build.

Only lit-bit runs cover pixels; zero bits/NONE write neither color nor depth.
Model/view/projection positions anchors in 3D, with bitmap dimensions in pixels.
Center offset uses Coin integer `(dimension-1)/2`, then bitmap raster rounding.
Qualified NVIDIA policy quantizes raster position to **1/2048 pixel**, then floors,
as observed through `GL_CURRENT_RASTER_POSITION`; it is not nearest-integer rounding.
Other implementations need their own precision qualification. Internal bitmaps use
**9×9** storage including margins of nominal 5×5/7×7 drawings.

MarkerSet honors startIndex/numPoints and repeats the last markerIndex in short
lists. Non-OVERALL materials follow occurrences from material zero, even with
nonzero startIndex. IndexedMarkerSet uses coordIndex, occurrence/materialIndex
bindings and coordinate-index fallback for unspecified indexed materials.
VertexProperty applies in a temporary scope. Capture follows user pre-callbacks
and preserves post-callbacks; subclasses retain virtual callbacks. Marker frames
are recaptured because global addMarker/removeMarker does not notify the graph.

## State, safe behavior and limits

Like native GL, markers disable lighting/textures while retaining color/alpha,
alpha test, depth test/write and raster fog. SCREEN_DOOR does not stipple bitmap
pixels and sets primary alpha one. Disabling texture does not clear inherited
image transparency classification: inspect inherited images (even disabled units)
for scheduling without transporting them as marker textures. Fully opaque RGBA/LA
stays opaque; alpha zero still classifies as transparent. Explicit SceneTexture
policies precede pixel inspection. Clip planes/view volume test the 3D anchor;
raster position also tests bitmap lower-left origin, retaining native disappearance
at left/bottom bounds.

IndexedMarkerSet's legacy GL infers alignment from marker ID. If replacement/
removal makes that incompatible with real registry stride, reject `UNSUPPORTED`
before publication; native FIXME behavior may read out of bounds. MarkerSet uses
real alignment and admits internal-marker replacement. Short indexed marker lists
repeat their last value like COIN_DEBUG; Release CoinGL lacks that bound, so this
safe extension is not compared against its undefined access. Missing positive
bitmap IDs are inert, matching IndexedMarkerSet's checked GL behavior; Release
MarkerSet's unchecked registry access is not certified. Refusal preserves the
previous frame. Custom VertexProperty and degenerate view volumes require another
raster contract and are rejected in this profile. Complexity BOUNDING_BOX uses
[box capture](coin-render-bounding-box-contract.md), retaining native per-type state.

Per-node limits: **1,048,576** coordinate occurrences, **4096** distinct bitmaps,
**16 MiB** copied bitmap bytes and **65,536** coverage runs/material variants.
Validate all read coordinates/indices/attributes. With external primitive observers,
also validate indices read by inherited point generation, including NONE occurrences.
Inherited classification allows eight units and **128 MiB** alpha-image bytes per
node capture. An inactive SceneTexture forcing transparency is rejected because
native classification can depend on a prior retained GL image.

## Scoped shadow/raster qualification

C07.1 qualifies no-map object/triangle BLEND/ADD ordering as whole markers with
sparse masks, alongside text/image, in seven Linux cells. C07.2 qualifies ordinary
composition of zero-map groups; native active CoinGL reference remains failing in
B21. C07.3 qualifies spot/directional casters with coverage rebuilt per light camera,
even outside observer camera; transparent material excludes casters. Marker BASE_COLOR
retains unlit color. C07.5 adds per-slot casters with **4/8** maps and BLEND/ADD sorting;
C07.6–C07.8 add RTT/physical window/depth/annotations with **1/4/8** maps. C07.9 adds
three planar-fog functions/multiple anchors. C07.10 has separate signed-caster,
receiver, slot/RTT/window/restore alpha-test profiles. C21.4/C21.5 qualify authored
shadowMapScene cameras and reset of pre-camera model with analytic displacement/
invariant frames; B21 native raster reference still fails. These qualifications
do not cover unlisted cross-products.

`CoinRenderMarkerSetTest` separates `--capture`/`--gpu`, with required CoinGL and
marker-free controls against vacuous comparisons. Original Linux/NVIDIA campaign:
**363 GPU comparisons** (121/executor) matched RGB exactly; paired CPU comparisons
had maximum **1/255**, maximum per-channel region MAE **0.333333**. Two captures,
**49** CTest regressions and **609** text/image/fragment comparisons passed without
final skips. Historical private ABI stayed **44**, common state **1648 bytes**.
Only the MarkerSet object was additionally checked with render OFF (not an entire
OFF library build). Other GPUs/Windows, framebuffer alpha and every host consumer
remain unqualified; retained subclass callbacks do not certify custom GL semantics.
Original counts, exceptions and campaign receipts remain in the
[archive](coin-render-evidence.md).
