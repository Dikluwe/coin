# CoinRender depth and polygon-offset contract

`CoinRenderAction` captures `SoDepthBufferElement` and `SoPolygonOffsetElement`
per render-state snapshot. Defaults: test/write enabled, LESS, range `[0,1]`,
offset disabled, factor/units zero and FILLED. Enabled offset preserves factor,
units and the style mask. Expanded lines/points retain their original style;
only intersecting styles receive bias. Subsequent states and Separator siblings
never inherit a previous submit's uniforms. Disabling test also disables effective
write, even with captured `write=TRUE`; original fields remain in the plan.
See [fragment policy](coin-render-fragment-policy-contract.md).

## BGFX and native bias precision

After clipping, the BGFX fragment shader originally used:

```
depth = near + (far - near) * gl_FragCoord.z
slope = max(abs(dFdx(depth)), abs(dFdy(depth)))
unitScale = rendererIsOpenGL ? 1 / 8388608 : 1 / 16777216
depth = clamp(depth + factor * slope + units * unitScale, 0, 1)
```

Every draw initializes these uniforms, including zeros for inactive/masked
bias. The same depth feeds fragment output, manual peeling comparison and
weighted-OIT weights. Fragment range mapping preserves the original clipping
volume; changing projection alone would reveal previously clipped geometry.
BGFX still owns test/write/function state. Opaque reordering admits only the
default state (test/write, LESS, `[0,1]`, no offset); any nondefault state preserves
the complete traversal order.

Native BGFX `setDepthControl(constant, slopeScale, clamp)`/GL polygon offset
would be overwritten by fragment-depth output and would not affect the
`gl_FragCoord.z` used for peeling. Shared shader depth therefore owns this
calculation. The constant term approximates D24: **2^-24**, or two D24 LSBs on
OpenGL because one can round back to the same value through fragment-depth
conversion. It is not bitwise native-GL parity on D32F or drivers with a different
minimum offset. Slope uses remapped depth. Applying original-style bias to
independent expanded lines/points extends GL_POLYGON_OFFSET_LINE semantics.
Fragment depth may reduce early-Z efficiency; this semantic work has no measured
host performance result.

The initial Rust path uses viewport range and `wgpu::DepthBiasState`, rounding
constant units to i32 and retaining float slope. Both values key pipelines;
style masks resolve before pipeline selection. Native Depth32Float precision
can differ from BGFX's D24 approximation. The private bridge changed **18→19**
by appending state fields with size/offset assertions; mismatched revisions are
rejected before state reads. Stable Coin node/libCoin ABI is unaffected.

## Captured portable extension (2026-10-06)

This extension supersedes the initial increasing-only range restriction in its
qualified profile. Clamp each finite range endpoint independently to `[0,1]`;
reversed and collapsed ranges are valid. Reject NaN/Inf before image/serial
publication. This does not add geometric depth clamp.

Core resolves original triangular-face window slope and maximum depth; polygon
styles use the original contour before stroke expansion. FILLED may split a draw
into per-triangle ranges while retaining order and annotation clear. Reject
primitives outside the original clipping volume. Resolution is idempotent.
CPU uses the D32Float quantum for units, including a representable subnormal
step; that numeric CPU control does not qualify subnormal preservation on every
physical shader/format. GPUs use their actual depth format. Coplanar controls
cover both signs, units alone, factor and normal/reversed ranges.

wgpu maps reversed ranges in fragment depth with viewport `[0,1]`, as for resolved
bias, preserving clipping before bias. This introduced private revision **48**,
without layout changes. Raw private plans with unresolved offset and reversed
range must first pass Core preparation; they are not the captured public profile.
BGFX uses explicit `near + (far-near)*fragmentDepth`, retaining constant ranges,
and evaluates derivatives only for nonzero factor. Coplanar strokes with active
test/write may receive an equivalent collapsed range. Original positions/Z
clipping remain; subpixel coverage changes do not alter bias or W/UV/color/fog.

## Tests and evidence limits

`CoinRenderDepthContractTest` covers capture/defaults, post-expansion masks,
state preservation, FFI and cache invalidation. GPU Vulkan/OpenGL controls cover
coplanar overlays, signs, disabling between frames, masks, ranges and readback.
The original 2026-09-26 BGFX delivery passed **47/47 CTests**, Rust **3/3**;
Rust GPU execution was not validated in that original delivery. Later captured
range/bias execution is scoped by the [geometry profile](coin-render-geometry-viewport-contract.md).
The [archive](coin-render-evidence.md) retains original commands, warnings and
staged evidence. Other formats/drivers, early-Z cost and real application
viewport validation require their own campaigns. Historical successful overlays
do not substitute for host integration evidence.
