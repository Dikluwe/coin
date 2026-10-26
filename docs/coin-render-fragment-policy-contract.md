# CoinRender depth, alpha-test and REPLACE policy

Implemented on 2026-10-05 against Coin/OpenGL semantics. Disabling
`SoDepthBuffer.test` disables both comparison and effective depth write even
when `write` is enabled. CPU, wgpu and BGFX retain original fields in capture
and apply this execution rule. Peeling's temporary attachments have their own
algorithmic policy. The discriminating scene draws far blue, near red with test
OFF/write ON, then intermediate green with test/write ON: green must win.

`CoinRenderFragmentPolicyTest --depth-only`/`CoinRenderDepthDisabled*` exercise
six frames: OFF/write ON, repeat, OFF/write OFF, OFF/NEVER, ON/write ON control,
and OFF restoration. OFF cases must have identical color/depth; ON stores the
near quad. Full finite GPU depth readback is mandatory; OFF/ON/OFF restoration
requires exact per-executor buffers. CoinGL comparison is RGB, without native
GL depth readback.

## SoAlphaTest

Capture adapts `SoLazyElement` without `GLRender`, preserving inheritance and
Separator state. The plan carries a semantic function/reference `[0,1]`; GL
enums remain in Coin adaptation.

| Function | Accepted fragment |
| --- | --- |
| NONE / ALWAYS | Always |
| NEVER | Never |
| LESS / LEQUAL | Alpha less than / at most reference |
| EQUAL / NOTEQUAL | Alpha equal / unequal to reference |
| GEQUAL / GREATER | Alpha at least / greater than reference |

Compare final alpha after textures/combine and before fog, depth write and blend.
Discard changes neither color nor depth. Clamp out-of-range references like GL;
NaN/invalid functions return `INVALID_SCENE`. With an effective test, normalize
primary material alpha at capture to Coin's `glColor4ub` 8-bit precision before
interpolation/textures: `0.5` becomes `128/255`, so `LEQUAL 0.5` discards it.
NONE/ALWAYS retain the prior contract. Native transparency classification and
SCREEN_DOOR values remain independent of this conversion.

`SoImage` and mono `SoText2` glyphs inherit the test; gray Text2 retains CoinGL's
own `GREATER 0.3`. Coverage masks still suppress empty raster regions. Surface
shaders share tests with RTT, peeling and weighted OIT. Effective BGFX tests
use the general path rather than restricted instancing. The original profile
rejects shadows combined with an effective alpha test before submission;
NONE/ALWAYS remain admitted. Later per-raster/shadow profiles have separate
qualification, including the marker C07.10 cases; do not generalize them.

Private wgpu revision **43→44** appended function/reference preserving earlier
offsets. At that Linux build common state **1640→1648 bytes**, FFI state
**2280→2288 bytes**; material GPU stride stays **80 bytes**. Projective UV later
introduced revision **45**, FFI **2292 bytes**. These are transport-history
facts, not stable public layouts; rebuild C++ and Rust together. No public Coin
class changed for this policy.

## REPLACE and classification

RGB/luminance `SoTexture2.model=REPLACE` replaces RGB and preserves preceding
alpha; RGBA/luminance-alpha replaces both. RGBA8 conversion must retain original
component semantics. Capture uses common combine: RGB from TEXTURE, alpha from
PREVIOUS for RGB/L, per unit. Explicit `SoTextureCombine` has precedence. No new
image format, digest or transport is required. Precedence gates place combine
before the active image, matching Coin examples; changing combine after the
image in traversal is not qualified by this campaign.

Fragment alpha and native shape classification are separate. Coin considers
all material transparency slots even when OVERALL uses only opaque slot zero.
This flag governs blending/defer in relevant modes even for NONE/ALWAYS and an
opaque REPLACE/combine output. Action NONE/SCREEN_DOOR retain their policies.
A discriminating gate draws RGB/REPLACE then a green depth-disabled quad: green
wins with opaque material; changing only an unused slot to transparency `0.5`
defers the red texture, which wins. Repeat/ALWAYS/used-slot/A/B/A controls verify
classification without changing texture/shading alpha.

## Evidence

The [test](../testsuite/coinrender/CoinRenderFragmentPolicyTest.cpp) has `--capture`
and `--gpu`; GPU mode requires valid CoinGL, permitting skip only when the GPU
executor is unavailable. RGB parity does not certify general framebuffer-alpha
parity; CoinRender retains separate source-over alpha semantics. Original Linux/
NVIDIA profiles: wgpu/Vulkan and BGFX/Vulkan/OpenGL. Initial **330** comparisons
(110/executor), then **348** (116/executor) for RGB/REPLACE passed at maximum
1/255; depth-only **18** GPU/CoinGL comparisons had zero RGB error. Original
logs, pilot ordering divergence and regression counts remain in the
[archive](coin-render-evidence.md), not a current HEAD rerun.

Peeling uses default algorithm depth without a native `write=FALSE` affecting
temporary collection. OIT uses one surviving contribution for ordered-blend
comparison. RTT's uniform producer/interior UV control proves alpha-test in
staged/direct producers, not filter edges, CLAMP or arbitrary RTT geometry.
Shadow rejection checks prior valid PHONG output and preserved publication.
Other drivers/Windows need the same gates in their own environments.
