# Public texture-sampling policy

Introduced on 2026-10-08. Each target has an immutable policy. NATIVE is default;
PORTABLE reconstructs only isotropic `NEAREST_MIPMAP_LINEAR`. Other filters retain
native semantics. It does not alter the installed driver.

```cpp
CoinRenderOptions options;
options.renderer = COIN_RENDER_RENDERER_VULKAN;
options.textureSamplingPolicy = COIN_RENDER_SAMPLING_PORTABLE;
CoinRenderCapabilities caps{};
if (coin_render_query_capabilities_for_renderer(
      COIN_RENDER_EXPERIMENTAL_OFFSCREEN, options.renderer,
      &caps, sizeof(caps)) != 0) { throw std::runtime_error("invalid query"); }
auto selection = coin_render_select_sampling_policy(
    &caps, options.textureSamplingPolicy, 1);
if (selection.reason != COIN_RENDER_SELECTION_SUPPORTED) {
  throw std::runtime_error("policy unavailable or unqualified");
}
auto target = std::unique_ptr<CoinRenderTarget>(
    CoinRenderTarget::createOffscreen(SbVec2i32(640, 480), options));
CoinRenderAction action(SbViewportRegion(640, 480));
action.setRenderTarget(target.get());
action.apply(scene);
// Check action/target status and publication before consuming output.
action.setRenderTarget(nullptr);
```

Initialize Coin/action first and query the renderer actually requested. Overloads
without options stay native. Scene manager/adapter accept options for offscreen/
Xlib and forward them; changing policy requires recreating adapter/target. Selection
is pure, without runtime initialization/environment/fallback. Qualified-profile
requirement **1** asks for bounded evidence; native remains available with **0**.
Policy enum values **0/1** differ from mask bits **1/2**. `COIN_SAMPLING_STUDY` no
longer selects shaders.

LOD uses derivatives of original UV before repeat/clamp/centering. POT uses the
active finer-mip texel center with hardware inter-mip blend; NPOT computes two
centers and blends two samples. Magnification stays linear. Dimensions come from
bound GPU resources, including DIRECT RTT. Producers inherit policy; target/plan/
producer mismatch rejects before submission.

The reconstructed filter is bit **1<<2**, maximum anisotropy **1**. Active higher-
anisotropy samplers reject without publishing, replacing read pointers or advancing
serial; later valid submission recovers. Mip-chain budget **128 MiB**, eight units;
other resource/device limits remain. Compiled formats: RGBA8, SRGB8, RGBA16F, BC3,
BC3 SRGB; this mask does not prove adapter format support. Alpha remains linear
in sRGB. CPU uses analytic derivatives, wgpu Vulkan fine derivatives, wgpu OpenGL
and BGFX Vulkan/OpenGL ordinary derivatives. LOD/raster boundaries may differ;
no universal GPU/CoinGL identity is promised.

Native/portable shader profiles are separate. wgpu shares device/resource caches
and lazily creates portable profile; untextured/instancing paths stay native.
Initial semantic delivery makes no performance claim. Capability V4 separates
`implemented_sampling_policies`, `available_sampling_policies`,
`qualified_sampling_policies`; code/test evidence for bounded Linux offscreen/
Xlib profiles does not certify current hardware automatically. Queries preserve
exact V1/V2/V3 prefixes. Mechanism selector still accepts V3; sampling selector
requires complete V4 and distinguishes invalid request, missing implementation,
runtime not ready, hardware absent and unqualified profile.

Adding the C++ options field requires rebuilding experimental consumers; binary
compatibility with old unversioned objects is not promised. Stable libCoin ABI
was not extended. Historical private revision **50→51** retained **456-byte** frame,
using final reserved word for policy; native uniform prefix **3040 bytes**, instance
payload **96 bytes**. Current paired declarations remain authoritative.

Historical Linux offscreen profiles were BGFX/wgpu × AMD Vulkan, NVIDIA Vulkan,
AMD OpenGL. **170/170 process executions**, not unique tests, were qualified:
CPU **2**, API **12**, portable-deep/direct-RTT/viewport/procedural **24**, native
selection/publication/ownership/advanced/RTT **30**, portable projective/sampling/
advanced/RTT **24**, native eight texture families **48**, resources/instancing/
shadows **30**. Rust/Naga **46/46**, C11 API probe PASS (V4 **584 bytes**, V3 **536**).
Broader AMD oracle cohorts used external Mesa reference; installed-driver native
CoinGL sampling divergence remains. Earlier ICD-path skips were corrected and
excluded from qualified totals. Final API/resource/shadow gates follow informative
BGFX capability correction; sampling code was unchanged. Hashes are archived.

Linux Xlib/manager/adapter and warmed host observations have separate receipts;
warmed evidence never certifies cold-start borders. The host integration itself
is not included in this generic submission. Windows/Android and remaining driver
studies retain their own status. See [evidence](coin-render-evidence.md); mutable
build directories are not historical binaries. Preserve source/binary hashes and
immutable baselines when reproducing; original campaign logs remain archived.
