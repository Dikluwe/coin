# Resolved common composition

`CoinRenderComposition.h` owns alpha classification, Coin modality, ordering and
effective depth. The former `exactCoin` switch is removed. BGFX, wgpu packing and
CPU consume the same decision; Rust validates/executes transport without choosing
passes from alpha or sorting objects by camera. Wiring captures sorting center,
explicit depth-field mask and layers for every executor. Core uses snapshots,
not SoState/path traversal or GPU commands. Native references are
SoGLRenderAction handleTransparency/renderSingle/doPathSort, SoDepthBuffer and
SoAnnotation.

## Common schedule

NONE/SCREEN_DOOR do not blend. Screen-door Bayer **32×32** preserves Coin bitmap
MSB order and global lower-left anchoring without horizontal mirror. It stipples
FILLED polygons only, not polygon LINES/POINTS or native lines/points. Measured
Mesa/NVIDIA CoinGL divergence in that crossing remains **B09.1**, with strict
native comparisons retained. ADD/BLEND execute immediately in traversal order;
DELAYED modes defer transparent draws preserving relative order. SORTED_OBJECT
sorts stably back-to-front by captured bounding-box center, before unsorted deferred
draws (fixture fallback uses depth-range center). SORTED_OBJECT_SORTED_TRIANGLE
also produces ordered triangle ranges, including packets sharing shape/instance;
weighted OIT never silently substitutes. SORTED_LAYERS_BLEND requests layers.
Annotations are per-layer/traversal ordered, including opaque/transparent mixtures,
and clear depth only in their marked viewport.

Deferred transparency defaults to test TRUE, write FALSE, LEQUAL, range `[0,1]`;
explicit SoDepthBuffer fields override these on native path reapplication.
Annotations use captured state. `CoinRenderCompositionItem` carries draw index,
blend/defer/add/sort/strategy/screen door/effective depth. CPU executes effective
depth even for immediate transparency; BGFX only adapts concrete representation.
See [transparency](coin-render-transparency-contract.md) and
[peeling](coin-render-peeling-contract.md) for equations and limits.

## Mono RGB manager and authored layers

`CoinRenderManagerAdapter` captures base and each superimposition as distinct
authored occurrences. Backgrounds precede base; foregrounds follow insertion
order. Six technical modes and `setTexturesEnabled` affect base only; layers keep
own flags/transparency/resources even for repeated subtrees. ZBUFFERON controls
test without inventing write masks. CLEARZBUFFER clears layer viewport even with
no geometry. Absent base does not invent clear in AS_IS. HIDDEN_LINE retains its
own color/depth clear after backgrounds, including null base with foreground;
a completely empty manager remains INVALID_SCENE.

Native first registered background consumes clear ownership even when disabled.
If no effective clear owner exists, these targets cannot perform persistent color
LOAD and reject UNSUPPORTED before capture/GPU. This includes null base/no BG and
first BG disabled even if base/another BG is active. Restoring/removing the first
background or restoring base recovers on same target. Background depth without
clear belongs to this target and may depend on previous frame; peers/resize/RTT/
optional attachment cache do not share history. MSAA retains per-sample depth,
not reconstruction from resolved readback. MONO doubleBuffer changes no result:
presentation belongs to target. Adapter creates no host GL context, applies no
host GL action and adds no preparatory GL render.

Synthetic technical passes of one base occurrence share its RTT capture. Distinct
authored occurrences preserve capture side effects; equal revision/payload may
share GPU execution only after capture. Admission failures preserve RGBA/depth/
serial before producer/root work and allow same-target recovery. Profile is Linux
mono RGB; color-index and framebuffer overlays without RGBA representation remain
unsupported. Layers do not remove real format/alpha-test/shadow/RTT-opacity limits.
Texture cascade derivatives execute before clip/stipple discard to preserve LOD;
masks/tests precede color publication. Physical visibility after successful present
remains subject to **B95**. Native stroke-junction limits remain separate.

## Stereo in a single RGBA target

Linux profile admits ANAGLYPH, INTERLEAVED_ROWS and INTERLEAVED_COLUMNS. One
monoscopic BG occurrence precedes the pair, base occurs once per eye and FG occurs
once afterward with all channels enabled. Technical modes/texturesEnabled remain
base-only. Native stereo clear can erase BG pixels without eliminating its
traversal/resources; synthetic passes do not repeat authored callbacks/producers.
Each eye completes all admitted mono composition in private GPU attachments before
output selection. ANAGLYPH uses LEFT red, RIGHT green/blue and depth. Alpha follows
native two writes: LEFT initializes alpha received by RIGHT, without transferring
RGB/depth between eyes. Retain that alpha per MSAA sample through final composition
and FG. Interleaving selects complete RGBA/depth per pixel, phase relative to logical
manager viewport including nested/odd dimensions. No CPU readback/reupload composes
views or normal presentation.

Admit both eyes/all producers before GPU work. A RIGHT-only authored failure
preserves RGBA/depth/borrowed storage/serial/camera and permits retry. Contain authored
exceptions long enough for CoinGL traversal cleanup, including temporary cameras;
restore prior mode/adjustment. Nonnotifying setters do not justify reusing another
eye's capture. Prepare/finish/sensors/hooks belong to one public frame, whose
publication serial advances once; queue serials/async tickets are separate domains.

STAGED producers execute per occurrence. Exclusive leases reuse execution target
only after copying each occurrence's pixels. Owner retains up to **four** idle targets
by LRU keyed by full size/format/mips/options/domain/generation. Resize/loss/destruction
invalidates leases without deleting children under registry lock. This is not
content caching; changed revisions still render. Eviction/recompilation does not
reject valid descriptors. Retention includes backend/attachment/buffer memory.
Scratch always reads its current execution even with optional readback pipeline
**2/3**, using existing slots/wait bound; consumer policy remains separate.

Private tokens are scoped to backend/device/transaction; eyes/authored occurrences/
producers/peers never alias because numeric slots happen to match. Stereo introduced
private ABI **70**, `CoinWgpuFrameView` **600 bytes**, appending tokens/mode/origin/
extent. Rebuild C++/Rust together. SEPARATE_OUTPUT/QUAD_BUFFER request native
LEFT/RIGHT buffers unavailable on one RGBA target, so remain explicitly UNSUPPORTED.

## Native BOX/LINE highlight

R35 synchronous Linux RGBA8 profile admits native BOX/LINE in MONO and the three
RGBA stereo modes. Each eye finishes technical base and highlight before stereo
selection; authored FG remains monoscopic afterward. Highlight retains own color,
width/pattern/visibility/alpha/clipping/depth. BOX computes bounds in current eye
camera/viewport. LINE replays selected paths in native order with ancestors and
selected subtree, without repeating off-path Separators. First SoSelection remains
selection source; shared occurrences/Switch identity stay intact. Generated replay
creates no extra RTT producers, shadow maps/casters/receivers; its purpose remains
distinct from authored layers, including plan reuse.

Admission includes replay/both eyes/resources. Active nonfinite styles, changed
paths during capture, replay-only bad indices or authored exceptions preserve old
publication and recover on same target. Internal search/bounds actions also finish
legacy cleanup before propagating failure. Invalid inactive/no-selection style is
inert. Hooks/serial still count one public frame; no host GL rendering occurs.
Base restrictions remain, e.g. BOUNDING_BOX extra texture units without persistent
GL coordinates remain rejected. Native runtime GL action types, native stereo
buffers, general async managers, HDR/other platforms are separate. **B34/B94/B95**
remain externally open without retries or increased physical observation deadlines.

GL oracle controls disable display lists that miss nonnotifying stereo setters
and normalize stencil upload/image origin/Bayer phase for aligned UPPER_LEFT
MSAA. Opaque RTT fixture is independent of outer channel mask. These controls
do not fix/certify altered legacy APIs. HIDDEN_LINE compares complete eyes:
legacy manager global clear/color-mask restoration does not define desired
composition. Eye selection additionally has independent all-pixel expectations;
stroke/edge numeric comparison retains mono P02/P07 limits.

## Transport, caches and borrowed schedules

Historical transport stages: revision **21** draw **56**, state **956**; revision
**22** state **1088** with clipping; revision **26** vertex **100**, state **2280**;
revision **28** frame **176** with options. Draw offset **36** is composition_flags:
blend bit **0**, additive **1**, screen door **2**, peeling **3**, stipple **8–14**.
Draw arrays are already ordered. Deferred depth variants pack distinct states
without mutating shared capture snapshots. Camera patch recomputes composition;
changed order/metadata revokes immutable-geometry hints. Packing failure invalidates
cached revision so next preparation rebuilds. These stages are historical, not
current header layouts; stable libCoin ABI is separate.

Common classification may prove opaque order for at least **256** triangle draws
without textures/samplers/shadows/annotations/strokes/screen door/transparency.
Publish proof only after classification succeeds; preserve inert sorting flags/
resolved depth exactly. Target still performs profile/admission checks. A proof
matching plan address/revision/policy permits a call-local immutable view used by
BGFX instancing and wgpu instancing/opaque schedules. Scope restores prior receipt
on every return, including failure. No composition reference enters backend cache,
Rust transport or async ticket. Revoking receipt revokes proof; other profiles keep
general vectors/algorithm. `COIN_RENDER_DISABLE_COMPOSITION_BORROW=1` restores both
copies for comparison. Trace separates logical copied/borrowed/computed item bytes,
not capacity/allocator traffic; `composition_identity.qualify_ms` includes existing
classification, not incremental proof cost. This historical optimization retained
private ABI **43**.

`CoinRenderCompositionTest`, DepthContract and FfiFrame tests cover eleven modes,
order/defaults/explicit fields, source-over/alpha/textures/ties, viewports/annotations,
rejection, camera reorder and invalid-packing recovery. Rust verifies execution
without reclassification, flags/barriers/layers/empty frames. Earlier fixtures
incorrectly drew LESS at clear depth=1, expected invalid material indices that
Coin clamps, or omitted valid offset style/flags; corrected fixtures are recorded,
not production fixes. CPU quantizes opaque alpha with clamp/rounding to avoid 254
from interpolated one while retaining nonopaque alpha.

Original A03 evidence: **16 wgpu + 9 BGFX Vulkan + 4 BGFX OpenGL CTests** with no
CTest skips, **7+1 Rust**. Optional lighting/fog CoinGL subcomparisons were unavailable
and reported SKIP even under Xvfb; they are not universal GL parity. BGFX/OpenGL
probe IDs **0/0** did not identify physical hardware. Later R29/R30/R35 evidence is
per-profile in the [scope matrix](coin-render-feature-matrix.md), with full original
receipts in the [archive](coin-render-evidence.md). A local profile closure never
certifies all manager modes, resources, drivers and cross-products.
