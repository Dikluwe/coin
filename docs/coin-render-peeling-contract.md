# Bounded depth peeling and weighted OIT (P10)

Peeling supports **1..8 transparent layers**, default **4**. Common policy operates
on captured frames; each executor implements its mechanism. Weighted OIT is an
explicit GPU extension, never automatic selection or silent layers replacement.

Action exposes `setSortedLayersNumPasses(int)` and
`setTransparencyBufferBudget(uint64_t)` with getters. Options enter plan/RTT children
and invalidate reuse/camera patches. `CoinRenderTransparencyCore.h` owns checked
limits/estimates. Default transparency-attachment budget **256 MiB/request**;
peeling needs `width*height*(16*layers+8)*samples`, weighted OIT
`width*height*24*samples` independent of peeling count. Invalid layer count/zero
budget is invalid; insufficient budget returns UNSUPPORTED before allocation/
publication, preserving image/serial and allowing valid recovery. Samples=1 on CPU;
no CPU fallback or silent MSAA reduction. Budget excludes geometry/textures/main
targets/tickets/driver/CPU allocator overhead and is not all targets' aggregate.
CPU vectors have additional overhead.

## Layer semantics and explicit depth

Select N nearest distinct transparent depths per pixel, then source-over from
back to front over opaque base, retaining base when more surfaces exist. Alpha
zero occupies depth/layer and does not stop search. Equal stored depths share a
layer, last winner under default LEQUAL. No fixed near-surface epsilon; stored
depth precision decides. GPU intermediates use RGBA16F/D32F, final RGBA8. wgpu uses
R8 masks/opaque-depth snapshot; BGFX R16F masks/layer and D32F opaque attachment,
also DIRECT RTT/window intermediates. BGFX layer cost **14 bytes/pixel**, within
16-byte common estimate; wgpu concrete **(13*layers+4)*samples** excluding main/
allocator. CPU byte conversion rounds to avoid repeated truncation. No bitwise,
HDR-output or sub-depth-quantum distance guarantee.

Evaluate all **8** captured depth functions against original opaque snapshot,
independent of internal layer-selection LEQUAL. Opaque draw without depth write
must not become an occluder. Test OFF also disables effective write. Publish depth
of nearest selected layer whose winning fragment has test+write active; otherwise
retain opaque depth. Per-fragment winner owns write mask, permitting mixed states.
CPU/wgpu/BGFX implement this in the C06 profile, superseding initial BGFX restrictions.
CoinGL SoDepthBuffer setOverride is not generic: explicit child fields win and ignored
fields inherit. Other modes/annotations retain their policy.

Private budget layout introduced revision **28**, retained through historical
**41**: FrameView **160→176 bytes**, count160/reserved-zero164/uint64-budget168;
then vertex100/state2280/draw56. Historical C06 used revision **59** without new
ABI. Direct private ingress checks the same conservative formula. Later revisions
require current paired headers; these are transport history, not stable libCoin ABI.

## Multisample peeling (R27.7)

Native linear RGBA8 executes **1×/2×/4×** without resolving intermediate layers.
Each sample selects its own layers, compares against original opaque sample depth
and retains winning write mask. Zero alpha/ties/truncation retain rules above.
Compose color/depth per sample before final resolve: average color, minimum depth.
VSM remains single-sample. Typed PEELING selects deferred source-over mechanism;
to mix local ADD/DELAYED_ADD, global action modality must allow those states,
e.g. SORTED_OBJECT_BLEND. Global SORTED_LAYERS_BLEND imposes native CoinGL override,
so local modalities cannot replace it. This preserves local additive ordering.

Preflight exact formats/renderability/sampleability/sample count/budget before
RTT producers; BGFX also checks all graph views. Window/GPU-only RTT depth is
sampleable from creation without extra readback. BGFX/OpenGL emits shader coverage
masks so preceding fixed mask cannot contaminate next MRT clear; every declared
sampler, including inactive shadow slots, gets correctly typed binding.
BGFX/Vulkan needs device sampleRateShading; stock local SDK disabled it and correctly
rejects 2×/4× before encoding. [Pinned SDK patch](../cmake/patches/README.md) negotiates
feature/capability without layout change, requiring matching consumer rebuild.
No vendor exception or global sample-shading activation. wgpu queries its own facts.

B48/B49 inclined-depth final campaign: **119/119** executions in seven available
Linux cells, **1×/2×/4×**, eight functions/range/offset/RTT/window. Independent geometric
oracle calibrates covered samples including edges; BGFX/OpenGL explicitly emits
opaque per-sample depth for the affected transition, Vulkan default retains hardware
depth. Same-profile restoration is exact; cross-format/path uses explicit
quantization bounds. This does not close B34, async/other formats/mips, translucent
bump/highlight-OIT or untested platforms/cross-products.

## Weighted OIT extension

`COIN_BGFX_TRANSPARENCY=auto` preserves Coin modality. Explicit weighted_oit (or
typed WEIGHTED_OIT in either GPU executor) accumulates deferred base source-over
only; immediate/additive passes remain, with deferred additive after OIT. Arbitrary
local-mode mixtures are not promised equivalent to GL. CPU rejects this extension.
For alpha A/normalized depth d:

```
weight = min(8, 8*A + .01) * min(16, 16*(1-d)^3 + .1)
accumRGB += RGB*A*weight
accumAlpha += A*weight
revealage *= (1-A)
```

Weighted mean uses opacity `1-revealage` and source-over onto opaque pass. wgpu
uses same formula with two attachments/separate resolve. This is a declared
approximation, not SORTED_LAYERS_BLEND parity.

## Evidence and limitations

`CoinRenderPeelingTest` uses captured scenes/fast paths, **10** surfaces in reverse
orders, per-face materials, count **1/2/4/8/1**, truncation, alpha
**0/.0001/1/255/.5/.9999/1**, zero-alpha chains, crossing/almost-coplanar surfaces,
saturation/halos, lit combine/textures, opaque write on/off, final alpha/depth and
rejection/recovery. Independent RGB/alpha tolerance **6** levels; depth **.003**.
Mandatory native GL control uses six surfaces/eight passes with no fallback.
Legacy GL counts opaque pass toward count and may drop opaque base on truncation;
common contract retains base plus N transparent depths, so ten-surface truncation
uses numeric oracle. Texture/combine/annotation exceptions are described in
[transparency](coin-render-transparency-contract.md).

Original 2026-09-28 RGBA8 offscreen/Xvfb closure: **52/52 BGFX**, **23/23 wgpu**,
Rust **13+2**, no final CTest skips. Weighted has independent many-surface/extreme-
alpha/order/material/light/texture/occlusion/final-alpha/additive controls. Shadow
extension P27.4 admits **1..8 maps**, PHONG/material/alpha texture/RTT ALPHA_BLEND
staged/direct, with mandatory shadow-reception GL controls and independent six-
surface/truncation/budget controls. Weighted remains approximate; native eight-map
GL availability is separately qualified. Distinct clear palette entries avoid
VSM/transparency collision; sampler reflection limits stay enforced.

C06 (2026-10-18) qualified **56/56** processes across seven AMD/NVIDIA Linux cells,
Native/CANONICAL_8BIT, **384 cases/cell/policy**, eight functions/test/write/1/2/4
layers/order/opaque write, plus mixed fragment state, ignored inheritance, range,
alpha/annotations/rollback. RTT direct retains an intact opaque peer. Physical
window stamps compare XGetImage without swapchain readback, opaque/peeling/opaque,
resize **64→96→65→64**, increasing serial. Baseline wgpu passed **12**, BGFX failed
**16** by refusing admitted states; candidate passed all **28**, plus **63**
regressions. Four mutations were detected: ignored function, missing transparent
write, repeated first layer, write with test OFF. This does not qualify unavailable
wgpu/NVIDIA OpenGL, every C09/C17 format/resource or external platform.
Full original profiles, pilots and receipts remain in the
[archive](coin-render-evidence.md); these are not new contribution-HEAD results.
