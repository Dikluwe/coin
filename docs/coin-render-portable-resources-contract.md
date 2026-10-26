# Portable shaders, spatial textures, bump and multipass

The implemented resource families below are qualified only in their recorded
profiles. Implementation does not qualify every combination. The
[scope matrix](coin-render-feature-matrix.md) and [archived full matrix](coin-render-evidence.md)
distinguish current limitations from historical delivery stages.

## Portable shader v1

Input is a typed, versioned, immutable product module with Core-verified signature.
Legacy GLSL/ARB/Cg, `gl_*` builtins, WGSL strings and BGFX binaries are not that
input. Wiring identifies active Coin program/parameters; application explicitly
associates an adaptation through `CoinRenderAction::setPortableShaderModule`.
Without adaptation reject before publication. No arbitrary automatic GL-source
translation or execution of node string content is provided.

Initial module changes triangle fragment color only. Position, clipping,
perspective interpolation, fog, alpha test, depth, composition, selection, layers
and shadows retain their owners. Input is lit primary color before fog/composition
plus captured homogeneous UV. Output is finite, unpremultiplied RGBA. Explicit
conservative metadata governs alpha classification; module cannot write depth,
move geometry or hide picking primitives. Discard would require another contract.

| Property | Limit/semantics |
| --- | --- |
| Expressions | Acyclic AST; f32 constants, vectors 2/3/4, uniforms, add/sub/mul/div, min/max/clamp, dot, 2D sample; no loops/recursion/compute/side effects |
| Uniforms | **4 KiB/module**, exact types/sizes; missing/extra/NaN/Inf diagnosed; division only by validated nonzero constant/uniform scalar; lowering owns padding |
| Complexity | **256 expressions**, depth **32**, at most **8 samples/8 images**, bound by logical identity/generation |
| Coordinates | Explicit units **0..7**, common projective-Q rules; Coin functions resolved before Infra |
| Samplers | Common quality policy, never reinterpreted by module |
| Identity | Version/AST/signature/profile digest; parameter/image revisions; cache includes device/generation/format/samples |
| Alpha | Potential variability forces conservative scheduling; opacity requires structural proof, never one frame's pixels |
| Failure | Known validation/compile incompatibility rejects before producer submission; later runtime errors preserve publication/recover explicitly |

Canonical full signature including names is at most **65,536 bytes**; generated
function at most **262,144 bytes**, excluding transport NUL. Check lengths before
reading/copying/allocating callback-declared source. Invalid association preserves
previous binding and auxiliary outputs. Logical module transport budget is
**32 MiB/frame and root/producer graph**, inclusive. Stored instance costs
`64 + 40*N + 4*P + S` bytes for descriptor, N IR nodes, P parameter floats and S
signature bytes. Repeated draws in one slot count once; distinct slots/producer
faces count separately. Validate full sum before compilation/producer execution/
publication, including private Rust ingress before first Core module call.
This is not total driver memory or replacement for program-cache/texture/attachment/
multipass budgets. R22 retained ABI65; later R23.3/R24.3 used **66/67**.

Core validates/orders one IR; Infra emits WGSL/BGFX variants, checks native limits
and fence-retains programs. There are no independent Coin semantic interpreters.
Diagnostics identify module/resource/operation/phase without exposing shader source
in normal UI. Tests require constant/parameterized colors, projective/eight bindings,
mutations/resize, clip/fog/alpha/composition/RTT and malformed IR/signatures/limits/
compile/loss/recovery. CoinGL is oracle only for demonstrated equivalent adaptation.

## Volume textures

R23 represents all slices/spatial mip levels in a physical 2D RGBA8 atlas with
logical `volumeWidth/Height/Depth/Columns/SourceComponents` and `volumeMipmapped`.
Physical width/height describe atlas and `mipmapped=false` prevents 2D atlas mips.
CPU/GPU manually perform nearest/trilinear and 3D mip selection/interpolation,
wrap S/T/R and PROJECTIVE STR/Q after perspective interpolation. One sampler/unit
supports mixed 2D/3D without additional bindings; never silently collapse to one slice.

Atlas columns are sufficient powers of two for slices; levels stack vertically.
POT axes **1..256** have **729** extents, physical maximum
**4096×5471×4 = 89,636,864 bytes** including mips/padding. Same maximum applies to
all **16,777,216** integer extents introduced in R23.2. Limit **128 MiB/published
image**, plus actual executor 2D extent limit; scene/conversion temporaries add CPU
memory. Each NPOT mip axis is `max(1,floor(axis/2))`; integrate entire preceding
voxel-cell areas in XYZ, including odd trailing rows/slices, then nearest-integer
round once per level, ties upward. POT bytes remain unchanged; padding must be zero.
Identity/dedup/reuse include metadata/bytes; capture-local atlas cache includes node
revision. The maximum 256³ test does not certify missing formats/combinations.

Historical private format **11**, revision **60**, encodes W−1 bits **0–7**, H−1
**8–15**, D−1 **16–23**, mips **24**. R23.3 revision **66** adds sRGB bit **25**;
**26–30** reserved zero, **31** general opacity declaration without changing alpha.
Revision **67** preserves this. Validate length/dimensions/padding/flags/RQ before
revision-cache/publication, including lines/points; reject invalid extents without
silent reduction. Format participates in identity; older revision 65 is explicitly
rejected by the sRGB transport. Old packVolumeAtlas symbol stays linear; typed
overload admits only linear/sRGB and preserves output on failure.

`storedTextureColorSpace` applies to SoTexture3 authored bytes: retain base bytes,
decode sRGB RGB per voxel before spatial/inter-level filtering, keep alpha linear.
Generate RGB mips by full-area integration in linear light and encode once/level;
alpha keeps integer mean/tie-up; linear path keeps prior bytes. Output is linear
RGBA8. Compression/depth/HDR spatial formats and anisotropy >1 require other
contracts. R23.1 historical **14** processes covered seven Linux cells/two sampling
policies, intermediate filters and R-only footprint. Native same-object already-
mipmapped quality transition **0.6→0.8** can retain prior filter (**C14 open**).
B14 corrected native upload's confusion of image dimension/prior enable state on
3D→2D. New platforms remain separate.

C17.2 keeps independent STRQ in up to eight mixed 2D/volume/cube units with shadows.
Shadow DIRECT_ST uses transformed STR without Q division; ordinary volume PROJECTIVE
divides STR/Q; cube uses STR and ignores finite Q including zero. Reject nonfinite
values, zero/singular cube direction and invalid projective Q before publication.
LOD uses actual triangle derivatives; precise QuadMesh fan uses legacy nonaffine
attribute weights. Offscreen/window/RTT coverage remains per-profile.

## Stored cubemaps

`CoinRenderCubeTextureCore` prepares six equal POT faces **1..2048**, logical order
**+X,-X,+Y,-Y,+Z,-Z**, original L/LA/RGB/RGBA components and linear/sRGB RGBA8 storage.
Keep base bytes. Each fetch decodes sRGB RGB before filtering; mips integrate in
linear light/encode once per level. Alpha stays linear with integer mean/tie-up.
Atlas width **2N**, sequential level/face/row texels; height ceiling(total/width),
trailing padding zero. Full maximum chain is **4096×8192×4 = 134,217,728 bytes**.
CPU projection/sampling validates descriptors before reading and preserves output
on failure. Axis ties prioritize **X/Y/Z**; filtering clamps within each face,
without seamless edges. Four quality bands/per-face mips extend native CoinGL's
fixed LINEAR min/mag without mips, verified numerically rather than claiming native
mip equivalence.

MODULATE/REPLACE/DECAL/BLEND retain original components; RGB REPLACE preserves
primary alpha, DECAL L/LA rejects. No wrap across faces, seamless or authored
SAMPLECUBE. Reject anisotropy >1, NPOT faces, compression/HDR/depth, missing/mixed
faces, pending filenames and bad metadata/bytes. STR receives full 4×4 texture
matrix once and perspective interpolation before face selection; finite zero/
negative Q does not divide STR; do not normalize per vertex. Zero/nonfinite direction,
including an interior primitive singularity, rejects before publication. Qualified
inputs include TextureCoordinate3, unequal direction lengths/perspective W, default
bbox XY (square tie S=Y/T=X) with nonzero resulting direction, CPU Object/Plane/
ReflectionMap/Environment callbacks in recorded cases. No implicit Reflection.
Default reaching zero rejects. Geometric NormalMap supplies a hardware-dependent
zero dummy callback here and rejects even if a matrix shifts it; preserve separate
raster evaluator. Active TEXGEN without CPU callback rejects; quality zero retains
disabling. Separator/unit/revision/reuse remain coherent.

Validate before same-revision caches at Target/CPU/BGFX/C++/Rust. BGFX reuse,
including CAMERA_PATCH, includes format/metadata/bytes; Rust TextureKey includes
format-12 metadata, so same revision/digest/payload cannot hide linear↔sRGB changes.
Orientation follows native asymmetric interior controls on AMD/NVIDIA. LOD uses
chosen-face projection derivatives; bilinear/mip interpolation is manual with
integer texelFetch in BGFX and u32 addressing in wgpu, not CPU-preprojected images.
MSAA k/N coverage applies to an observed resolved face without subsequent mixed
texels; producer+consumer resolves can yield k/N² fractions. Isolate stages or
calculate complete composition, without vendor pixel patches. Axis ties/seams
are not seamless claims; native Q divergence remains **B29**.

## Cubemap RTT

One logical producer has six Core-described camera passes, earlier dependencies,
maximum eight levels and cycle rejection. Publish only all six complete faces
from one generation; sixth-face failure retains previous cube/frame/tickets and
fence-retires candidates. Cache includes face camera/scene/format/dimensions/
transparency/revisions. A window consumes the graph; it is not a face producer.
Each face clears background RGB with alpha **1** like native prerender callback.
Preserve authored camera parameters while replacing capture orientation without
mutating scene fields. Logical face order is independent of native enum numbering.
Own/inherited image override applies only unit zero, following active SoTexture2;
extra units do not set global override. This deliberately differs from native
SoSceneTextureCubeMap's inactive own-override setter paths.

Format **12** introduced in private revision **61**, retained through **67**:
face exponent bits **0–3**, source components **4–6**, mips **7**, GPU resource
**8**, sRGB **9**, reserved-zero **10–30**, opacity declaration **31**. RTT cubes
are always linear RGBA8, bit9 zero even with authored sRGB policy: never decode
rendered linear bytes again. GPU cube uses four components, null CPU pointer,
zero length; private content_digest carries token. Validate physical extent,
domain/generation/chain before cache. Revision/digest are hints, not resource
identity; descriptor/bytes/sampler/matrix remain validated. Recheck retained layout
against capture; canonical no-mip declaration can consume a shared complete atlas.
Reject repeated face/atlas tokens before retiring temporaries, preserving prior
publication and unique owner retirement. This is not public cube export API.

Assembled atlas token owns resources independently of six face tokens. DIRECT
retires face registry entries after assembly; physical destruction still follows
last-use fence and pending memory remains budgeted. Capacity uses peak live
records: k retained producers need **k+7** while assembling cube and leave **k+1**.
wgpu registry has **64** slots minus external live tokens; do not assign that
limit to BGFX. Reject zero/old-generation/aliased/inconsistent tokens and CPU
payload on GPU-resource descriptors. Account six colors, transient depth, mips,
resolve and simultaneous staging; 2D RTT's 64 MiB debit is not total cube memory.
Use checked sums/products, executor overhead/capacity and per-subresource pitches/
origin/face order; a 2D RGBA readback does not represent a cube.

RECORDING public STAGED linear RGBA8 **1×** selects CPU for root and all producers,
without automatic GPU→CPU fallback. Replacing only GPU consumer with a CPU fixture
does not prove this profile. Historical **33/33** headless tests cover faces/cameras/
sampler/alpha/graph/callbacks/sharing/stereo and dimensions **1/2/2048** with/without
mips. Root OOM after complete atlas retains publication and sets TARGET_ERROR;
explicit public same-size resize invalidates publication/serial and recovers same
target/backend with exact RGBA/depth A/B/A. Thread-local internal sixth-face fault
lets five real submissions finish, then fails before allocation/publication; root
retains output/readiness, disarming permits immediate B then exact A restoration.
Admission checks every face before any producer, so invalid last-face input does
not consume armed fault. CPU DIRECT/MSAA2×/4×/window/async/shadows/bump remain refused.

## Legacy opaque bump (R26)

Linux GPU profile: opaque triangles/linear RGBA8, **1–8** consumed lights. Without
shadows allow directional/point/spot mixtures; legacy spot uses position as point
without cone/attenuation, following soshape_bumprender::initLight. With shadows,
only directional lights/maps, up to **8** maps/one group. Never truncate to first
light or replace bump with ordinary Phong. Per vertex, transform light/eye to
object S/T/N basis in Coin normalization order; fragment interpolates, evaluates
DOT3 and specular exponent `shininess*64`. Sum contributions; base modulates
diffuse separately from specular. Nonbump occurrences retain own model. CPU provides
capture/admission/math controls, not bump raster execution.

Opaque bases: existing 2D RGBA8, volumes **1..256**, stored POT cubes **1..2048**,
2D/cube STAGED or DIRECT RTT. Models/combine/units0..7 follow receiver admission.
Decode base sRGB before filtering/combine. Normal-map RGB is linear encoded data,
never sRGB-decoded; ignore its alpha. Height conversion preserves native Sobel/
components. RTT face/atlas/mips remain linear with private bit9 zero. ABI67 needs
no new fields. Effective base/material alpha requires separate transparency contract
and rejects in this opaque profile; RTT's explicit declaration stays separate.
Opacity checks all sampleable mips/faces, excluding padding. Keep family sampler/
wrap/quality/P07-Q limits, including upload-associated quality and B29. Bump axes
**1..32767**, **48 MiB/image**, **256 MiB RGB/frame** transport.

Revision/digest/pointer never replace current resource validation. Admission precedes
cache/producer work, even malformed inactive bump data, final indices/lights/images
or sixth faces. Refusal preserves pixels/depth/serial/prior valid ticket; FFI publishes
only a complete successful candidate. Valid inactive data must not light another
draw. Recorded profiles cover MSAA1×/2×/4×, Native/Portable, offscreen/window/readback/
resize, directional shadows and RTT, without qualifying untested products. Physical
window observation uses original five-second deadline after one apply, no artificial
reapply/wait for another frame. No immediate first-XGetImage promise. Translucency/
OIT, arbitrary same-draw modules, PBR/HDR/depth/compression/seamless/other platforms
retain separate contracts. B34 recreation remains unqualified.

## MSAA, resolve and multipass

Target sample/format/color-space configuration has exact capability checks and no
silent reduction. Default is linear RGBA8/one sample; implemented initial profiles
use **1/2/4**. Historical P24 proposal for 8/sample-shading, HDR and general pass
extensions is preserved in the archive and is not implementation qualification.
Resolve linear color once after final writer, before RTT sampling/readback/present;
sRGB publication conversion is explicit. HDR requires float/exposure/tone-map contract.
Depth semantics must be stated per implemented profile; do not infer central/min/max
universal equivalence from an old unsupported-depth proposal. Current peeling
per-sample depth resolves minimum as specified in its contract. VSM stays single-
sample. OIT/peeling/alpha-to-coverage do not activate merely because MSAA is chosen.
Coin picking stays geometric.

Pass dependencies forbid simultaneous read/write of one subresource; Core orders
DAG/rejects cycles and preserves annotations/foreground/shadow/RTT order. Capture
once rather than repeating callback side effects per sample. Nominal attachments
use `w*h*(bytesColor*N + bytesDepth*N + bytesResolve)` plus simultaneous mips/OIT/
peeling/staging/tickets, with checked arithmetic before creation. Resize fence-
retires old generation; old-sized candidates cannot publish into new target.
Zero-size suspension allocates nothing; restore rebuilds before present.

R28 action/scene-manager `setNumPasses(int)`/get default **1**, retaining requested
value. At apply/render, <1 returns INVALID_SCENE, >**16** UNSUPPORTED before
producers/consumer. Adapter forwards native count; passUpdate with multiple passes
rejects because profile publishes only final result. Per-pass callbacks/other
native options remain separate R34 scope. Capture once/logical update with exact
reuse allowed. Apply native jitter to each occurrence's original effective-viewport
projection. Materialize Marker/Text2 after jitter before snap/layout; Image keeps
native nonjittered anchor. Camera-independent RTT/VSM executes once. GPU accumulates
float RGBA and publishes linear average once; depth comes from last sample. Target
MSAA1/2/4 remains independent.

Multipass budget default **256 MiB**, set/getMultipassBufferBudget; zero permits
one pass but rejects multipass OUT_OF_MEMORY. Admit simultaneous scene/MSAA/resolve/
float sums/OIT/peeling/VSM/RTT/staging/mips/output/readback/retained tickets/pending
queue resources before producers/root, with checked math. Refusal retains exact
RGBA/depth/serial and same-target/action/manager recovery. Budget is not all driver
memory or replacement for resident textures. Historical ABI**65**, FrameView**544**,
adds count/reserved-zero/budget and per-sample/state MVP float counts; one sample
uses null pointer/count zero. composition_flags **15..30** select samples (zero=all),
bit**31** marks materialized raster; lower bits retain prior meaning. Reject inconsistent
metadata before uploads/submission; never mix ABI63 headers/binaries with ABI65.
Subsequent bridge revisions, including stereo70/600, require matching recompilation.

## Host adaptation

Host callbacks must prepare cold content and update camera/viewport/DPR/material/
revisions without GLRender cache preparation. A subclass inherits no native visual
certification merely by type; custom callbacks remain observable. Known GL-only
behavior requires explicit host adaptation or rejection, since C++ cannot safely
introspect all overrides. Synthetic Coin tests/exports do not certify host consumers.
Record cold start without prior GL frame, visible output, resize/camera/DPR,
mutation/removal and picking/selection where relevant. Linux observations cannot
qualify new Windows code. Original case/cell matrices and receipts remain in the
[companion archive](coin-render-evidence.md).
