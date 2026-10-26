# Projective texture coordinates

Capture retains homogeneous `(s,t,r,q)` in each of eight units. Promote 2D to
`(s,t,0,1)` and 3D to `(s,t,r,1)`; `SoPrimitiveVertex` and Coin's 4D element keep
all components without capture-time division. `SoTextureMatrixTransform` applies
the complete matrix, including R and variable Q. Clipping interpolates all four;
common line/point expansion preserves them. Texture Q is independent of geometric
W and stroke `screenSpaceW`.

Classic executors perspective-interpolate `(s,t,q)` and sample `(s/q,t/q)` per
fragment. Vertex division is a different result for varying Q: a counterfactual
gate must visibly distinguish it. RTT vertical flip follows division. Capture
belongs to Coin adaptation; transform/validation/sampling policy are common;
executors transport and execute. Affine Q=1 retains the no-division path.
IndexedFaceSet's fast path accepts explicit 2D UV including projective matrices;
3D/4D input uses native callbacks to preserve R/Q. Deduplication, digest,
attribute comparison and reuse include these fields.

## Native shadow-shader policy

The generated `SoShadowGroup` shader uses transformed ST directly without Q
division. Capture records `DIRECT_ST` when a group is active and `SoShadowStyle`
includes SHADOWED; other draws use `PROJECTIVE`. This is independent of map count
and pipeline. Entry resets ShadowStyle to CASTS_SHADOW_AND_SHADOWED; Separator
pop restores it. SceneTexture producers start independent state with shadow
shader disabled; consumer policy survives return.

An empty `SoShaderProgram` inside an active group removes the generated program
and changes lighting/shadows. This profile returns `UNSUPPORTED` and preserves
the previous frame. Outside a group an empty program is admitted. Explicit active
shaders require their own portable adaptation contract.

## Admitted domain

PROJECTIVE transformed Q must be nonzero, nonsubnormal and same-sign within each
primitive. Consistent negative Q is valid; separate points may have differing
signs. Validate before clipping/expansion, including LINES contours, so clipping
does not hide a singularity between endpoints. Captured STRQ, matrices, transformed
values and ST/Q must be finite representable floats. Invalid input returns
`INVALID_SCENE` before publication, retaining pixels/serial and permitting recovery.
DIRECT_ST samples only transformed ST, so zero/subnormal/sign-changing Q causes
no division; captured values/matrices and transformed ST must still be finite.
Rust FFI consumers receive the same validation. Procedural/default UV, formats,
filters, quality, shadows and RTT retain their separate limits.

## Private transport and Coin ABI

The historical UV change introduced private wgpu revision **45**:

| Structure (Linux build) | Before | After |
| --- | ---: | ---: |
| Common vertex | 100 | 164 bytes |
| Common state | 1648 | 1648 bytes |
| wgpu FFI vertex | 100 | 164 bytes |
| wgpu FFI state | 2288 | 2292 bytes |
| BGFX full vertex | 188 | 220 bytes |
| BGFX compact vertex | 124 | 124 bytes |

The FFI vertex carries STRQ per unit; the policy is appended to FFI state and
uses former padding in common state. Material stride remains **80 bytes**.
wgpu uses **13** vertex attributes; BGFX uses free tangent/bitangent channels for
extra Q, with **15** attributes in the full layout. Instancing layouts remain
unchanged. Full-vertex memory growth is real, with no performance claim; budgets
use `sizeof`. Rebuild C++/Rust together.

This work remains on Coin 4 without incompatible `origin/coin-5` changes. Private
revision 45 is not libCoin's public ABI version. Homogeneous attributes are
private renderer state, with no UV change to `include/Inventor` or
`experimental/include`. `SoLazyElement::LightModel` is the canonical public enum;
`CoinRenderLightModel` aliases the same type, values and layout. The library
remains **4.0.10**, SONAME **libCoin.so.80**.

Historical Linux/GCC compatibility evidence against `v4.0.10` used equivalent
DWARF builds, libabigail **2.4** and an old-header external consumer loading the
new library without recompilation. The public-type comparison reported zero
changed types and zero removed/changed existing functions, only added symbols.
The complete report's two indirect differences changed neither public members
nor sizes; private types stayed opaque without custom suppressions. The old
consumer passed **14** checks and all three UV GPU gates passed. This qualifies
that Linux binary scope, not general source compatibility or MSVC/macOS.

## Oracle and qualification limits

[CoinRenderProjectiveUvTest](../testsuite/coinrender/CoinRenderProjectiveUvTest.cpp)
separates `--capture` and `--gpu`. GPU requires CoinGL; comparison measures RGB,
not general framebuffer alpha. This NVIDIA fixed-function reference exposes
`GL_MAX_TEXTURE_UNITS=4`; native comparisons cover up to four stages. Unit 7
alone with REPLACE uses an explicitly identified equivalent CoinGL scene on unit
0, retaining image/matrix/coordinates. Eight simultaneous stages instead use an
independent per-pixel bilinear homogeneous/product oracle, with per-unit A/B/A
and premature-division controls. This is not native eight-stage CoinGL evidence.

Shadow controls use a real spot map with zero intensity and white emissive
material to isolate sampling. They cover group styles/off, ignored DIRECT_ST Q
and empty-program refusal. This executable profile requires a visible pass;
zero-map groups remain outside it. Peeling/weighted controls use one layer/
contribution and do not claim arbitrary multilayer equivalence.

Classic peeling uses an explicitly identified equivalent CoinGL BLEND single
contribution over black. Legacy SORTED_LAYERS_BLEND ARB `TEX` ignores projective
texture environment and adds texture RGB to primary color; it is not the classic
projective oracle. Shadow controls use generated GLSL and native layers comparison.
RTT staged/direct controls have projective producer/consumer, separate Q mutations
and orientation. SceneTexture precedes consumer matrix: native FBO inherits outer
matrices, while pbuffer and current capture start independent state. External
producer-matrix inheritance is not qualified; pilot differences stay archived.

Historical Linux/NVIDIA wgpu/Vulkan and BGFX/Vulkan/OpenGL campaign: **414** native
comparisons, **12** unit-7 projections, **15** equivalent classic peeling controls,
**150** eight-unit analytic comparisons; regressions **348** fragments, **279**
text/image, **363** markers, **58** Core/CTest and **38** Rust/ABI/shaders.
Final GPU runs had no skips. Original pilots, ABI report and receipts remain in
the [archive](coin-render-evidence.md). Windows/other drivers need their own gates.
