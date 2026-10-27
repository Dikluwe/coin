# CoinRender maintainer guide

CoinRender is an opt-in experimental renderer for Coin scene graphs. It captures
Coin traversal into a common frame plan consumed by BGFX, Rust/wgpu, or the
auxiliary RECORDING executor. CoinGL remains the historical compatibility
reference for valid scenes. The experimental SDK requires explicit installation.

Start with [build and tests](coin-render-build.md), [architecture](coin-render-architecture.md),
[source layout](coin-render-source-layout.md), [compatibility policy](coin-render-compatibility-policy.md),
and [current scope and qualification](coin-render-feature-matrix.md).
The [proposal draft](coin-render-proposal.md) introduces the design, implementation
inventory and contribution boundaries for maintainer discussion.
[Evidence and archived documents](coin-render-evidence.md) explain the local
companion archive and the distinction between historical receipts and current tests.

The normative contracts are deliberately separate from dated campaign reports:

| Area | Contract |
| --- | --- |
| Selection, capabilities and options | [Selection](coin-render-selection-contract.md) |
| Geometry, viewport and depth | [Geometry/viewport](coin-render-geometry-viewport-contract.md), [depth](coin-render-depth-contract.md), [clipping](coin-render-clipping-contract.md) |
| Native styles and raster shapes | [Draw styles](coin-render-draw-style-contract.md), [bounding boxes](coin-render-bounding-box-contract.md), [markers](coin-render-marker-contract.md) |
| Fragment and texture semantics | [Fragment policy](coin-render-fragment-policy-contract.md), [projective UV](coin-render-projective-uv-contract.md), [multitexture](coin-render-multitexture-contract.md), [sampling API](coin-render-texture-sampling-api.md) |
| Resource admission and rendering profiles | [Portable resources](coin-render-portable-resources-contract.md), [material resources](coin-render-material-resource-contract.md) |
| Composition and transparency | [Composition](coin-render-composition-contract.md), [transparency](coin-render-transparency-contract.md), [peeling](coin-render-peeling-contract.md) |
| Targets, RTT and publication | [Multiple targets](coin-render-multi-target-contract.md), [RTT ownership](coin-render-rtt-ownership-contract.md), [RTT publication](coin-render-rtt-publication-contract.md), [window capture](coin-render-window-readback.md) |
| Reuse and host lifecycle | [Geometry overlays](coin-render-geometry-overlay-contract.md), [external frame preparation](backend-independent-frame-preparation.md) |

These documents describe admitted profiles and failure behavior. A feature bit,
compiled path, or historical passing cell does not qualify every driver, API,
platform, window system, scene combination, or current revision. See each
contract's limitations before interpreting its evidence.

## Remaining submission work

The consolidated development branch is `coin-render`. Historical integration
branches are preserved in a local Git bundle and archive refs rather than kept
as competing development branches. Local FreeCAD/Quarter adaptations remain
outside this contribution.

Existing-file changes inherited from the integration still require their own
Coin correction reviews before a final upstream patch series is selected.
The independent pre-PR studies must distinguish necessary renderer connections
from fixes to CoinGL, callbacks, notifications, geometry and VRML. In particular,
the inherited `SoBumpMapElement::source` member requires a public class-layout
and subclass-compatibility review; exported-symbol comparisons alone are not
proof of ABI preservation. This organization work does not close that review.

Publish the companion evidence at a durable external location and update its
links before asking remote reviewers to reproduce historical results. Windows
DX12, OpenGL and Vulkan testing must identify the final revision and dependency
versions; no new Windows qualification follows from the source reorganization.
