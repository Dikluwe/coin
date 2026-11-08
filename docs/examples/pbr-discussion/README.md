# CoinRender PBR discussion examples

Rendered from implementation commit d940a4b67179f423406a72a3962283f82eb90612 using BGFX/Vulkan and Mesa lavapipe. These are actual CoinRender readbacks, not simulated illustrations or CoinGL screenshots.

`render.cpp` uses the public CoinRender API. Build against the headers and libraries from that commit (CoinRender, CoinEnvironment and Coin), then run:

```sh
mkdir -p frames
coin_render_ibl_baker studio studio.cibl
./render frames studio.cibl
```

The baker is in `examples/coinrender/coin_render_ibl_baker.cpp`. Output files 0.rgba through 7.rgba are tightly packed 360 x 320 RGBA8. PNG panels preserve those pixels and add captions below them. No relighting, exposure edits or image enhancement were applied after rendering.

Panels: classic-and-pbr = 0,1,2; pbr-materials = 2,3,4,5; pbr-environment = 6,7.

Same sphere, orthographic camera and white directional light in all frames. Background black. Phong uses diffuseColor (0.025,0.18,0.55), specularColor (0.6,0.6,0.6), shininess 0.65. PBR dielectric uses baseColor (0.025,0.18,0.55); metal uses (1,0.71,0.29). PBR factors are linear, with exposure 0. These are illustrative material choices, not a fitted Phong/PBR equivalence or a performance benchmark. No shadows, AO or scene-object reflections are enabled.

The experimental PBR profile is opaque and BGFX-specific. CoinGL PBR, wgpu PBR and asynchronous owned-snapshot semantics are not demonstrated by these images.
