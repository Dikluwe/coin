# Coin transparency modalities (P09)

Common composition classifies alpha after texture/combine, resolves order/defer,
annotations/stipple/effective depth and expands triangle-sort ranges. CPU/BGFX/
wgpu consume that schedule without another Coin interpretation. Wiring captures
state, centers and layers; Core uses snapshots. Native sources include
SoGLRenderAction handleTransparency/renderSingle/doPathSort, primitive-cache
triangle sorting, SoGLLazyElement, SoAnnotation and SoDepthBuffer.

| Modality | Order and operation |
| --- | --- |
| NONE | Immediate traversal, no blend, preserve surface alpha |
| SCREEN_DOOR | Immediate/no blend, material-alpha polygon stipple |
| ADD | Immediate SRC_ALPHA, ONE |
| BLEND | Immediate source-over |
| DELAYED_ADD / DELAYED_BLEND | Opaque first, transparent traversal order |
| SORTED_OBJECT_ADD / SORTED_OBJECT_BLEND | Back-to-front captured center |
| SORTED_OBJECT_SORTED_TRIANGLE_ADD / BLEND | Object order then triangle mean-vertex depth, including materials sharing shape/instance |
| SORTED_LAYERS_BLEND | Per-pixel depth layers, back-to-front composition |

Sorting ties are stable. Do not reorder artificial stroke triangles or raster
pixel runs; images/text/markers retain original-object order. CoinGL retains raster
GLRender rather than replacing bitmaps with primitive cache (B19/C07.1).
Annotations stay traversal-ordered in own layer with depth barrier, outside base
peeling. Deferred default is test TRUE/write FALSE/LEQUAL/range `[0,1]` unless explicit
captured fields override. Immediate draws use captured depth; zero alpha may
still write depth. Reject unsupported concrete states before publication.

Source-over RGB is `Cs*As + Cd*(1-As)`, separate alpha `As + Ad*(1-As)`.
Additive RGB is `Cs*As+Cd`, alpha `As*As+Ad`, clamped in RGBA8 like
SRC_ALPHA/ONE. Separate source-over alpha is existing CoinRender behavior,
not a claim of exact legacy-GL alpha parity. Screen door uses Coin Bayer **32×32**,
`{0,2,3,1}`, global lower-left origin and **65** levels `int(transparency*64)`.
Primary alpha becomes one before texturing; texture/combine alpha remains. No
stipple on lines/points, including expanded strokes.

CPU collects per-pixel fragments; BGFX/wgpu execute actual layer passes and GPU
compositors. No substitution with object sorting or weighted OIT. BGFX native
profile uses `COIN_BGFX_TRANSPARENCY=auto`. Historical P09 private revision **27**,
later **28**, frame **176**, vertex **100**, state **2280**, draw **56**; flags:
blend0/add1/screen-door2/peeling3/stipple8–14. Both sides rebuild together. Rust
validates flags without reclassifying alpha; ordered triangle packets cannot
reuse one shape-identity entry. Camera changes recompute order/revoke old hints.
These are historical layouts; stable Coin ABI is unchanged by this transport.

## Multiple textures with shadows

Full GPU path keeps **8** scene units and up to **8 VSM maps** during transparency.
Copy moments into independent RGBA32F array layers, preserve each map resolution,
use four-texel bilinear receiver sampling and largest physical extent with per-layer
valid-region clamp. Native/Portable scene sampling is independent. BGFX slots
scene0–7, compositor depth8–9, array10; wgpu full layout has eight samplers/up to
11 textures, avoiding 18 textures. Array copy costs a BGFX view included in graph
admission. Never silently reduce maps/units/samples/mechanism.

Physical shadow budget **128 MiB/apply**: source RGBA32F+D32F **20 bytes/texel**,
plus **16 bytes/texel/layer** at largest extent. Allocate at least two native array
layers. wgpu six logical maps require **7** physical layers because installed GLES
HAL infers a cube from six square faces; extra unmapped layer is reserved consistently
on every adapter and counts in budget/layer limits. Check before RTT producers
and, on wgpu windows, before WSI acquisition. Format/binding/interstage/program
limits also gate admission; rejection preserves publication.

RTT keeps sampler/alpha policy. At quality **0.1**, native SceneTexture FBO uses
LINEAR where stored static texture uses NEAREST. NONE supplies sampled alpha one;
`gpuOpaque` remains a conservative stored-content proof and may be false. DIRECT
transports token/metadata, STAGED pixels. Classification uses sampling policy and
captured state. Recorded Native **1×/2×/4×** profiles require Native for MSAA2×/4×
in this path; transparency/depth/RTT still obey concrete executor limitations.
C02.9/C03.6 evidence is not all resource cross-products.

## Tests, historical limits and oracles

`CoinRenderTransparencyTest` covers eleven modes, fast paths, alpha
**0/.25/.5/1**, polygons/styles/native strokes, independent RGB (up to five levels),
**1024** stipple pixels, material sorting/intersections, framebuffer alpha, unit7
combine-alpha replacement, annotations and NEVER overrides. Composition/depth tests
add explicit/default fields, ties, barriers and rollback. With
`COIN_RENDER_REQUIRE_GL_REFERENCE=1`, native GL must keep requested mode, no fallback.
Basic modes/material sorting/intersections compare native RGB. Peeling with
combine/multitexture or annotations retains mandatory GL execution but uses independent
numeric expected output: native ARB handles only texture0, replaces its environment/
combine and has differing annotation behavior. Separate framebuffer alpha also
uses numeric expectation rather than classical GL blend.

Original P09 fixed-four layers, BGFX epsilon/zero-alpha stop and restricted
LESS/LEQUAL/NEVER + write-FALSE base-peeling were historical stages. The current
[peeling contract](coin-render-peeling-contract.md) supersedes them in qualified
profiles with **1..8** distinct transparent depths, zero-alpha continuation,
no fixed epsilon and C06 explicit-depth execution in CPU/wgpu/BGFX. Those updates
do not retroactively certify every older P09 comparison. Original P09 covered at
most two transparent surfaces over opaque background, not all ties/almost-coplanar/
zero-alpha chains. Other ten modes/annotation fields retain independent contracts.

Native POINTS triangle sorting may repeat a tessellation-shared vertex; Core keeps
one point/original face vertex. Controls sample an unambiguous corner, not universal
native duplication parity. Styles/MSAA/offset/clamp/external-viewports and hosts
need their corresponding matrix cells, as do extra drivers/platforms/prototypes.
Original 2026-09-28 closure: wgpu **22 CTests**, BGFX **47/47**, Rust **12+2**, no final
CTest skips, mandatory Mesa GL controls with declared exceptions. Existing
SCREEN_DOOR fixture expectation was corrected. Xvfb/Mesa does not identify every
physical driver. Original logs/pilots are [archived](coin-render-evidence.md).
