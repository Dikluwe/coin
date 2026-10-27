# CoinRender source layout

CoinRender has a separate optional library and package. Its public declarations
live in `include/Inventor`; build with `COIN_BUILD_RENDER=ON` and install the
experimental SDK only with `COIN_INSTALL_RENDER_EXPERIMENTAL=ON`. Default Coin
header installation excludes CoinRender experimental headers. Use the canonical `CoinRender` package and build options; obsolete CMake option
and package aliases are excluded from this contribution.

| Source area | Responsibility |
| --- | --- |
| `include/Inventor` | Public experimental declarations |
| `src/actions/` | Coin action, traversal and state adaptation, split into Action/Execution/Reuse/Capture/Rtt implementation files |
| `src/rendering/coinrender/` | Common Core, plans, target orchestration, backend contracts and wiring |
| `src/rendering/coinbgfx/` | BGFX executor, private BackendInternal state, resources, submission, shaders and lowering |
| `src/rendering/coinwgpu/` | wgpu C++ connector, private FFI, modular Rust bridge and shaders |
| `testsuite/coinrender/`, `testsuite/coinbgfx/`, `testsuite/coinwgpu/` | Common and executor tests |
| `testsuite/coinwgpu/rust/unit/` | Rust unit tests |
| `testsuite/coinwgpu/rust/gouraud_shader.rs`, `shadow_shader.rs` | Rust shader integration tests |
| `examples/coinrender/` | Standalone examples, benchmark clients and installed-SDK consumer |
| `cmake/patches/` | Pinned external dependency patches and reproduction instructions |

The auxiliary CPU rasterizer is compiled for RECORDING and test builds only;
production BGFX/wgpu builds exclude it. Lines and points share one WGSL source,
while retaining their separate pipeline topologies and shader-module instances.
See the [source-size breakdown](coin-render-size.md) for the contribution scope.

Private headers stay beside implementations in `src`. Non-template bodies
belong in `.cpp` files. Header definitions are appropriate for templates and
small inline accessors whose contract requires visibility, without exposing
private implementation types through public declarations.

`CoinRenderAction` is Wiring: it reads Coin state, controls occurrences,
callbacks, original contours, layers and capture errors. It does not include
executor classes or choose behavior through BGFX/wgpu macros. The factory owns
compiled executor selection. `CoinRenderPlanAssemblyCore` receives snapshots,
matrices and arrays, normalizes/deduplicates state and assembles indexed ranges;
it does not read `SoAction`, `SoState` or nodes. `CoinRenderCubeGeometryCore`
shares immutable 24-vertex/36-index cube ranges, validates local indices and
limits its material cache to 32; the builder decides Coin eligibility and
bindings. `CoinRenderImageCore` expands L/LA/RGB/RGBA to RGBA8 transactionally;
quality, wrapping, texgen and RTT producer selection remain in capture.

`CoinRenderTransformCore` owns projection conversion to depth `[0,1]`, normal
matrices, viewport projection/scissor intersection and finite-matrix checks.
BGFX lowering and the wgpu packer use the same calculations. Compact/full vertex
layouts, uniform packing, batching and buffer patches remain executor-specific.
Transparency ordering, alpha, screen door and effective depth are common Core.

`CoinRenderTarget` owns admission, dimensions, generations and transactional
publication. `CoinRenderNativeSurfaceCore` checks descriptor ABI, size, reserved
fields and handles without platform SDKs. `CoinRenderBackend` owns per-target
execution. `CoinRenderBackendRuntime` supplies services that may outlive targets:
surface support/destruction, ticket polling/cancellation, telemetry and device
polling. The runtime service has process lifetime but does not own host handles
or retain GPU devices itself; concrete resources remain in Infra. Surface
release precedes wgpu executor destruction and never destroys the host window.
Ticket polling/cancellation works after the originating target is destroyed.
BGFX detach discards its executor and advances generation; CPU depth initialization
is an explicit executor fact, not a compile-time guess in the target.

Builds and consumers must recompile experimental C++ types and the paired
C++/Rust private bridge together. Historical ABI numbers in contracts document
specific changes; the checked-in FFI declarations and assertions determine the
current transport. Do not mix historical headers and a newer bridge library.
See [build and tests](coin-render-build.md) and [archived extraction evidence](coin-render-evidence.md).
