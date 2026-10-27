# CoinRender experiment: optional BGFX and wgpu rendering backends for Coin — feedback welcome

> Unpublished draft. The `coin-render` branch is currently local. Add verified
> public branch/document links before posting; no branch or issue is published
> by preparing this text.

I have been experimenting with an optional rendering path for Coin. It captures
a frame description during normal Coin traversal and renders it with BGFX or
Rust/wgpu backends. I would like to share what has been implemented and ask
whether this direction could be useful to the project.

A brief overview of the current prototype:

- **Source:** `coin-render`; public branch URL pending publication.
- **Opt-in:** disabled by default (`COIN_BUILD_RENDER=OFF`).
- **Toolchains:** C++11 for public API consumers, C++17 for the implementation;
  Rust and Cargo are required only for the wgpu backend.
- **Dependencies:** BGFX with bx, bimg and shaderc, or wgpu 24 through Cargo's
  lockfile. The prototype includes external BGFX patches and modified vendored
  Naga, wgpu-hal, wgpu-core and glow sources. These changes need their own review
  and maintenance plan; they are detailed in the
  [dependency guide](../cmake/patches/README.md).
- **Size:** approximately 68,000 first-party source/shader lines across capture,
  shared code and both backends, plus 141,000 test/support lines. Counts include
  comments and blank lines and exclude vendored dependencies.
- **Coin 4 ABI:** not established. The prototype adds `SoBumpMapElement::source`
  to an existing class; I am treating this layout change as ABI-breaking for
  Coin 4 and would redesign it before proposing a compatible integration.
- **GPU validation:** earlier results cover specific Linux/Windows revisions;
  the consolidated revision still needs fresh GPU qualification, including
  Windows DirectX 12, Vulkan and OpenGL. CPU/shader checks are not GPU results.

The [architecture document](coin-render-architecture.md) includes a Mermaid
workflow graph; the [feature scope](coin-render-feature-matrix.md) describes the
implemented functionality and its limits, and the
[source-size breakdown](coin-render-size.md) explains the current scope.
Coin's existing `SoGLRenderAction` renderer remains its current OpenGL renderer.
FreeCAD motivated some of the work, but its application patches and Quarter
integration are outside this proposal.

The implementation was developed with AI assistance. I am responsible for the
contribution, its verification and addressing review feedback.

I am not asking for CoinRender review ahead of the current PR queue; I would
like to understand whether this work, or a smaller part of it, is worth pursuing
as a contribution once those PRs have progressed.

Does this direction seem useful to Coin? Are there existing plans or alternative
approaches I should consider before taking the work further?
