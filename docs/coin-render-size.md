# CoinRender source size and contribution scope

CoinRender is a scene capture layer, a shared rendering implementation and two
GPU backends. It includes transparency algorithms, RTT, spatial textures,
shadows, stereo, resource recovery and reuse. It is larger than a graphics API
adapter that only forwards geometry to a library.

This explains the responsibilities in the source tree; it does not prove that
every remaining line is essential or that the whole tree belongs in one PR.

## Source breakdown after cleanup

| Area | Text lines | Responsibilities and existing checks |
| --- | ---: | --- |
| Coin action | 5,926 | Traversal, state, callbacks, capture, reuse and RTT wiring; `CoinRenderActionTest` and portable callback/abort/update tests |
| Common rendering | 26,964 | Geometry and appearance rules, plan assembly/validation, composition, targets, resources and managers; Core, interning, depth, composition, RTT ownership and manager tests |
| BGFX | 13,058 | 9,588 C++ implementation lines, 1,289 private-header lines and 2,181 shader/template lines; lowering, resources, passes, presentation and readback |
| Rust/wgpu | 22,062 | 15,722 Rust lines, 4,715 C++ connector/header lines and 1,625 WGSL lines; paired transport, encoding, pipelines, resources, surfaces and readback |
| **Total** | **68,010** | Includes both alternative GPU backends and the auxiliary CPU reference |

Counts include comments and blank lines. They cover `.cpp`, `.h`, `.rs`, `.wgsl`,
`.sc`, `.sh` and `.in` files in `src/rendering/coinrender`, `coinbgfx`, `coinwgpu`
and `src/actions/CoinRender*`. Vendored dependencies, public headers outside those
directories, examples, tests, build scripts and documentation are excluded.
These are source-text counts, not compiled instruction counts or a complexity
score. A single GPU build does not compile both backends.

## Removed duplication and inactive paths

- Lines and points had two 516-line WGSL files with identical bodies. They now
  use one source. Their shader-module instances, topologies and pipeline cache
  keys remain separate; the cleanup does not merge GPU pipeline identities.
- The old Dawn/wgpu-native experiments created and cleared attachments but did
  not encode scene geometry. Their source, CMake choices and private wiring were
  removed from the contribution. They remain recoverable in Git history.
- The 1,363-line auxiliary CPU rasterizer is compiled only for RECORDING or test
  builds. Production BGFX/wgpu libraries exclude it. Its source remains necessary
  for explicit CPU controls, so it is still included in the source-size total.

The first-party source count fell from 68,868 to 68,010 lines: **858 lines removed,
about 1.25%**. Excluding the CPU rasterizer from GPU production builds is a
separate binary-scope improvement, not another reduction in source lines.

## What still deserves review

The largest shared implementation is `CoinRenderFramePlanBuilder.cpp` at 4,654
lines. It captures many node/state families; splitting it by responsibility may
help review but would not reduce the implementation itself.

`CoinWgpuFfiFrame.cpp` has 2,454 lines because it owns transport buffers, packs
several resource kinds and maintains reuse paths. Paired C++/Rust declarations
and boundary validation are required for the current bridge design, but their
maintenance cost should be reviewed explicitly.

BGFX and wgpu share scene semantics and common transforms. They still need
different resource, shader, synchronization and presentation implementations.
Deleting either backend would materially reduce the source count and also remove
an implemented option. That is a scope decision rather than dead-code cleanup.

Common rendering plus action wiring still totals **32,890 lines**. Therefore a
small initial Core/RECORDING PR cannot simply include every shared feature.
An initial contribution should select a limited geometry/material/target profile,
then introduce advanced composition, spatial textures, shadows, stereo and reuse
in later slices. The full implementation remains the development source while
those review slices are prepared.

Source consolidation, successful builds and CPU/shader checks do not establish
fresh GPU qualification. The per-platform limits remain in the
[scope index](coin-render-feature-matrix.md).
