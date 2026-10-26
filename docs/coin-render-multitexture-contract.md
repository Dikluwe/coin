# Multitexture and strokes (P08)

This profile shares scenes, numeric expectations and CoinGL controls across CPU,
BGFX and wgpu. It does not close all draw-style, binding, procedural-UV or
transparency combinations. [Projective UV](coin-render-projective-uv-contract.md)
extends its original transport with STRQ and per-fragment division, distinguishing
four-stage native CoinGL references from eight-stage analytic controls.

Wiring enables `SoTextureCombineElement` and uses `SoTextureCombine::doAction()`
field interpretation without querying GL hardware during capture. Capture includes
per-unit image/sampler/matrix/coordinates/combine. SoLineSet endpoint details
advance with vertices, including callback/picking semantics.
`CoinRenderTextureCombineCore.h` validates/compiles four vectors per unit,
normalizes operations/arguments, supplies CPU execution and a conservative alpha
proof. It alone interprets Coin enums; BGFX/wgpu execute the program with their
own shaders/resources. `CoinRenderStrokeCore.h` owns expansion.

## Semantic profile

- Eight units **0–7**, including sparse units and disabled unit zero, with
  independent image/sampler/matrix/explicit UV.
- RGB operations: REPLACE, MODULATE, ADD, ADD_SIGNED, SUBTRACT, INTERPOLATE,
  DOT3_RGB and DOT3_RGBA. Alpha supports the first six. Independent scales
  **1/2/4**, clamp after each stage and constant RGBA clamped to `[0,1]`.
- Sources: PRIMARY_COLOR (pre-cascade color), TEXTURE, CONSTANT and PREVIOUS.
  RGB operands support color/alpha and complements; alpha supports SRC_ALPHA/
  ONE_MINUS_SRC_ALPHA. Disabled units leave PREVIOUS unchanged. DOT3_RGBA
  replaces alpha, ignores the alpha operation and retains its scale.
- FILLED, polygon LINES/POINTS and native lines/points use the same program.
  UV, color/alpha, W and fog distance survive expansion. UV/color perspective
  interpolation remains; points keep vertex UV without point-sprite semantics.
- Solid/patterned lines share unit-fragment assembly. Rounded width replicates
  coverage on the minor axis. Rounded integer point size has minimum one and
  different even/odd center placement. An isolated line/point blends once per
  covered fragment.
- Native line phase continues per polyline and resets at qualified Coin resets.
  After clipping, Core starts at first visible coverage; GL permits an
  indeterminate initial phase on clipped lines.
- Invalid operations/scales, extra UV indices or excess units fail before
  publication, retaining pixels/serial and allowing a corrected scene. Separator
  pop restores combine state.

Aliased raster assembly follows OpenGL 2.1 §§3.3/3.4/3.8.13 and Coin source. Line
comparisons admit the specified one-coordinate/unit-fragment alternatives rather
than bitwise identity; matrix points are compared pixel-by-pixel.

Historical private wgpu revision **26**: vertex **100 bytes**, state **2280**, unit
**96**, draw **56**; extra UV offset **44**, extra units **1096**, combine programs
**1768**. Both sides assert sizes/offsets and reject malformed programs/nonfinite
values. These are historical layouts; rebuild the current C++/Rust pair. Stable
Coin ABI is unaffected.

## Tests and limitations

`CoinRenderMultitextureTest` covers operations/sources/operands/scales against
independent tables with fast paths on/off and five geometry forms; eight active
units, varying UV/matrices, sparse units, LineSet details, state restore and
rejection/recovery. Raster controls cover seven line directions/positions, widths
**0.75/1/2/3/4/6.6**, masks **0/ffff/000f/aaaa**, alpha **0.5**, fractional point
centers, clipping and empty publication. Draw-style controls also cover unit-seven
UV with W **1–4**, matrices, models, fog, bindings and contours.

Combine fixtures use RGBA, explicit UV and linear sampling. Native tests use
canonical order TextureUnit → TextureCombine → Texture2 → shape. Formats, legacy
models, quality and procedural/default UV retain their own contracts. Shared
surface shaders do not qualify every object/OIT/peeling combination, driver,
MSAA, external viewport or host integration.

Historical 2026-09-28 evidence: wgpu Debug/C++20 **21** CTests; BGFX Release/C++11
**44** distinct CTests after correcting six surface fixture expectations; Rust
**10** unit and **2** WGSL tests. Earlier failures stay recorded. Reproduce with
an appropriate build and valid Mesa/GLX reference:

```sh
env __GLX_VENDOR_LIBRARY_NAME=mesa COIN_GLXGLUE_NO_PBUFFERS=1 \
  COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_RENDER_REQUIRE_GL_REFERENCE=1 \
  xvfb-run -a ctest --test-dir <build> -R '^CoinRenderMultitexture' --output-on-failure
```

BGFX `_vulkan`/`_opengl` fixtures fix renderer/object mechanism. Shared surface
`CoinBgfxSurface_.*` controls use their historical
`COIN_WGPU_REQUIRE_GL_REFERENCE=1` variable. Mesa/llvmpipe success is not physical
hardware certification. Original receipts are in the [archive](coin-render-evidence.md).
