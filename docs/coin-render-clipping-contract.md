# CoinRender clipping contract (P01/F03)

Implemented on 2026-09-28: up to eight active planes per captured state/draw,
for triangles, lines and points. BGFX/OpenGL, BGFX/Vulkan and Rust/wgpu execute
the common contract. The Dawn/native prototype rejects clipped triangles before
submission. Implementation support is separate from device qualification.

`SoClipPlane::doAction` adds the plane when `on` is active or ignored; its callback
uses that path during capture. `SoClipPlaneElement::add` uses the model matrix
at the plane node. `get(i, TRUE)` returns a world-space plane; later shape
transforms do not redefine it. Planes accumulate in state; Separator pop restores
them. CoinGL sends `(normal.xyz, -distance)` and keeps the side
`normal.dot(position) - distance >= 0`. Sources are `src/nodes/SoClipPlane.cpp`,
`src/elements/SoClipPlaneElement.cpp`, and `src/elements/GL/SoGLClipPlaneElement.cpp`.

Wiring captures `clipPlanesWorld` per state. Deduplication/payload comparison
include plane contents; changes invalidate camera reuse. Recording logs all four
equation components. `CoinRenderClipCore.h` converts world planes into camera
space without reading actions/state or commanding a GPU. It clips line segments
and tests point centers before quad expansion, interpolating clipped endpoint
attributes. Expanded states do not clip strokes again. State equality includes
plane semantics.

BGFX applies the equations in the shared surface shader for object, weighted OIT
and sorted-layer passes, including lighting, texture and auxiliary opaque depth.
Triangles retain vertices and lighting/texture interpolation. Camera patch with
active planes rebuilds because BGFX vertices also contain camera-space positions.

The private wgpu transport introduced clipping in revision **22** (state **1088
bytes**). Historical revision **26** had **2280 bytes** after multitexture/combine
and original-depth bias fields, with clipping offsets unchanged and draw **56
bytes**. `clip_plane_count` is at **956**, `clip_planes` at **960**. The C++ packer
uses the same Core; Rust validates transport and supplies WGSL uniforms shared by
standard/line/point shaders. Rust does not rediscover Coin planes or spaces.
These are historical layouts; rebuild the checked-in C++/Rust pair together.

Nine or more planes return `UNSUPPORTED`, without truncation, pixel replacement
or serial advance. A later valid request can render. Nonfinite planes, zero
normals or singular view matrices are invalid. `COIN_RENDER_FEATURE_CLIP_PLANES`
describes implementation, not universal platform support.

`CoinRenderClipPlaneTest` covers transformed capture, disabled planes, Separator
restore, copy/reuse, camera equations, pre-expansion segment/point clipping,
opaque/transparent pixels, eight planes, camera movement, mirrored shapes,
perspective, lighting/textures, depth, limits and recovery with indexed fast path
on/off. BGFX fixtures distinguish OpenGL/Vulkan × object/weighted_oit/sorted_layers.
Rust checks excess payload, zero normals, NaN and shader validity.

Historical closure: **41 CTests** (18 wgpu, 23 BGFX), **8 Rust unit tests and 1
shader integration** passed; Debug/wgpu and Release/BGFX builds completed.
Optional CoinGL offscreen references were unavailable even under Xvfb and were
reported as internal skips, so this does not claim full CoinGL image parity.
Local GPU profiles were NVIDIA RTX 3060 Laptop Vulkan and BGFX/OpenGL under
Xvfb, RGBA8 offscreen, tolerance six byte levels at sampled points. Receipts and
original narration are in the [companion archive](coin-render-evidence.md).

Open qualification includes GL boundaries, patterned strokes, MSAA and real host
scenes; additional devices/platforms/surfaces and RTT dependency/publication
combinations. More than eight planes and arbitrary projective transforms are
outside this profile. Clipping creates no section caps and does not itself
implement draw styles or text. P01 implementation closure does not close F03's
entire qualification matrix.
