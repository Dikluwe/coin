# Building and testing CoinRender

Use an out-of-source CMake build and Coin's normal platform dependencies.
Coin headers and consumers retain the C++11 minimum; CoinRender implementation
uses C++17 privately. Private diagnostic/failure-injection hooks are compiled only when both
`COIN_BUILD_TESTS` and `COIN_BUILD_RENDER` are enabled; normal non-test builds
exclude them. The default configuration leaves
CoinRender disabled. These commands are reproduction recipes, not new test
results for the contribution snapshot.

```sh
cmake -S . -B build-recording -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RECORDING \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build build-recording --parallel 2
ctest --test-dir build-recording --output-on-failure
```

`RECORDING` builds without Rust or a GPU executor. It provides contract tests,
logs and auxiliary software rasterization, not universal visual qualification.

For wgpu, install Cargo/rustc and use the checked-in locked bridge dependencies:

```sh
cmake -S . -B build-wgpu -DCMAKE_BUILD_TYPE=Release \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=RUST_BRIDGE \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build build-wgpu --parallel 2
ctest --test-dir build-wgpu --output-on-failure
CARGO_TARGET_DIR="$PWD/build-wgpu/rust_target" \
  cargo test --offline --locked --release \
  --manifest-path src/rendering/coinwgpu/rust_bridge/Cargo.toml \
  -- --test-threads=1
```

CMake supplies `CARGO_TARGET_DIR` inside its own build tree and invokes Cargo
with `--locked`. Offline Cargo testing requires the dependencies to be available
locally. Serialized Rust tests are the historical safe host recipe: the earlier
parallel library suite passed 42 assertions but could crash at process shutdown,
also with the untouched baseline. That result is historical, not a new claim
that every current concurrency/platform profile passes.

BGFX requires an installed CMake package exporting `bgfx::bgfx`, its matching
`shaderc` program and `bgfx_shader.sh`. Use the pinned revisions and patch
instructions in [dependency reproduction](../cmake/patches/README.md):

```sh
cmake -S . -B build-bgfx -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/bgfx-sdk \
  -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND=BGFX \
  -DCOIN_BUILD_TESTS=ON -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build build-bgfx --parallel 2
ctest --test-dir build-bgfx --output-on-failure
```

The recovered Windows dependency patch reproduces its pinned SDK source hashes;
it does not qualify a new Windows build or fix outstanding OpenGL/device issues.
`DAWN` and `WGPU_NATIVE` are experimental prototypes requiring their corresponding
SDKs; they are not substitutes for qualified BGFX/RUST_BRIDGE profiles.

To build the common window example or comparison tools, add
`COIN_BUILD_RENDER_WINDOW_EXAMPLE=ON` or `COIN_BUILD_RENDER_BENCHMARKS=ON`.
Select these only with the required platform/window and CoinGL dependencies.
The [examples guide](../examples/coinrender/README.md) identifies their purpose.

To test installation, configure a separate build with
`COIN_INSTALL_RENDER_EXPERIMENTAL=ON` and `CMAKE_INSTALL_PREFIX` set to a disposable
SDK directory, build and run `cmake --install`. Build
`examples/coinrender/sdk-consumer` against that prefix. Also configure the default
Coin build with CoinRender OFF and verify its SDK does not expose CoinRender
experimental headers. Installing the opt-in SDK does not stabilize its ABI.

GPU tests require the actual requested API/device and, where specified, a valid
CoinGL reference context. Xvfb alone does not guarantee one. Historical NVIDIA
reference runs used `COIN_GLX_PIXMAP_DIRECT_RENDERING=1` and the appropriate PRIME
selection. A skipped reference comparison is not a pass; retain test diagnostics,
renderer/device/driver identifiers, source revision, build flags and command lines.
Run the per-feature CTest gates named in the contracts and record exact counts,
skips and failures. Performance tools require image validation before FPS claims.
See [evidence interpretation](coin-render-evidence.md).
