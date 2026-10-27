# Proposal: an optional CoinRender library for Coin

This is a discussion draft for introducing the implementation and agreeing on a
reviewable contribution plan. CoinRender is experimental; this proposal does not
request replacement of CoinGL or stabilization of the experimental API.

## Motivation and design

CoinRender captures a Coin scene into a common frame plan and executes that plan
through BGFX or Rust/wgpu. This makes Coin scene semantics reusable across graphics
APIs while keeping API-specific resource handling in the executors.

The implementation has three responsibilities:

1. **Coin integration:** `CoinRenderAction` traverses nodes, reads state and
   callbacks, and captures occurrences. Its implementation is divided into action,
   capture, execution, reuse and render-to-texture files.
2. **Shared Core:** scene-independent data, plan assembly, transforms, geometry,
   texture rules, composition and validation. These components do not traverse
   Coin nodes or call BGFX/wgpu.
3. **Execution:** each executor owns its buffers, pipelines, shaders, passes,
   native presentation and readback. BGFX and the Rust bridge are divided into
   modules by responsibility.

`CoinRenderTarget` controls admission, dimensions, resource generations and
transactional publication. Backend runtime services provide surface and readback
operations without transferring ownership of the application's window.

BGFX and Rust/wgpu are alternative executors selected at build time. DirectX 12,
Vulkan, OpenGL and Metal are graphics APIs, subject to executor and platform
support. Options can request a supported API without silent fallback. Switching
BGFX and wgpu dynamically within one binary is not implemented.

RECORDING is an auxiliary executor for diagnostics and CPU controls. Its results
do not establish GPU image parity. CoinGL remains the historical compatibility
reference for valid Coin scenes, alongside independent mathematical controls.

## Implementation inventory

The following mechanisms are implemented with explicit admitted profiles. Each
entry has limits; it does not imply every combination works on every executor.

| Area | Implementation |
| --- | --- |
| Geometry and state | Shared indexed geometry, captured bindings, native draw styles, contours, clipping, viewport, depth and polygon-offset policies |
| Appearance | Materials, legacy lighting, Gouraud interpolation, fog, alpha test, projective coordinates, multitexture and combine rules |
| Sampling and spatial resources | Explicit native/portable sampling options, mipmaps, stored linear/sRGB volume and cube textures |
| Composition | Coin transparency policies, bounded depth peeling, optional weighted OIT, layers and depth barriers |
| RTT | Producer DAG, STAGED/DIRECT routes, resource ownership and generation checks, cubemap producers, atomic result publication |
| Shadows and bump | Captured shadow groups and multi-map plans; bounded legacy bump-lighting profiles and selected resource combinations |
| Raster content | Native bitmap text, images and markers, with explicit composition/resource profiles |
| Managers | Mono rendering, technical modes, authored background/foreground layers, stereo composition and BOX/LINE selection highlights |
| Antialiasing | Admitted MSAA profiles and multipass rendering with bounded resources |
| Lifecycle | Multiple targets, asynchronous readback tickets, cancellation, recovery and publication rules |
| Performance | Plan reuse, camera/geometry updates, interning, batching, instancing and resource caches with admission checks |
| Consumers | Standalone examples, installed-SDK consumer, native surface connectors and a separate browser capture/instancing demonstration |

The [scope and qualification index](coin-render-feature-matrix.md) gives the
per-feature restrictions and separates implementation from historical validation.
The [maintainer guide](coin-render.md) links the detailed contracts.

## Contribution boundaries

- CoinRender is a separate optional library, enabled by `COIN_BUILD_RENDER`.
  Installing its experimental headers/package requires
  `COIN_INSTALL_RENDER_EXPERIMENTAL`. Default Coin installation excludes them.
- Existing Coin files need a small integration boundary, but inherited independent
  Coin fixes still require separate review and selection for the upstream series.
  The `SoBumpMapElement::source` layout/subclass compatibility review remains open.
- FreeCAD/Quarter patches, application macros and their local integration harness
  are excluded. Generic manager, callback, RTT and surface mechanisms remain part
  of CoinRender even where a local application helped exercise them.
- Production builds exclude private fault-injection controls. Such controls are
  available only in explicit test builds.
- Large validation artifacts and dated campaign documents are preserved outside
  the source contribution. The [evidence guide](coin-render-evidence.md) explains
  the companion archive; a durable external publication is still needed.

## Validation and proposed discussion

Historical branch and working-tree differences have been reconciled against the
consolidated implementation. Earlier material indexing, stereo assembly and
restricted resource profiles have newer implementations in the common Core and
executors. Restoring the earlier files would remove those extensions. This
reconciliation does not establish runtime equivalence on untested platforms.

Known external and functional limits, including B34/B94/B95, remain visible in the
scope index. The final consolidated revision still requires Windows DirectX 12,
OpenGL and Vulkan qualification; historical Windows results do not qualify it.
The experimental C++ API and paired private C++/Rust transport must be rebuilt
together.

The initial discussion can establish whether the optional-library boundary and
common-plan design fit Coin, which independent fixes should precede integration,
which initial executor/platform subset maintainers want to review, and how to
split the implementation into reviewable pull requests.
