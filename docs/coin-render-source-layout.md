# CoinRender source layout

CoinRender remains an opt-in library with a separate build target and package.
Its public declarations live in `include/Inventor` so applications can include
headers using the established Coin paths. Their location does not make them
part of Coin's stable default API: build them with `COIN_BUILD_RENDER=ON`, and
install the CoinRender SDK only with `COIN_INSTALL_RENDER_EXPERIMENTAL=ON`.
The default Coin header collection and install explicitly exclude all 17
CoinRender and compatibility-alias headers. The `CoinWgpuExperimental` alias
remains available for existing build integrations.

Public API declarations and implementation stay separate. Public `.h` files
belong under `include/Inventor`; private headers belong beside their
implementation under `src`. Put non-template implementations in `.cpp` files
in their owning source area. Header definitions are appropriate for templates
and small inline accessors whose bodies must be visible to callers or whose
existing contract explicitly requires inline behavior. Keep those exceptions
narrow and avoid exposing private implementation types through public headers.

The architecture keeps Coin traversal and backend work in distinct areas:

- `src/actions/` integrates Coin actions, state and traversal.
- `src/rendering/coinrender/` owns shared planning, targets, contracts and
  orchestration.
- `src/rendering/coinbgfx/` owns the BGFX executor.
- `src/rendering/coinwgpu/` owns the Rust/wgpu executor and bridge.
- `testsuite/coinrender/`, `testsuite/coinbgfx/` and `testsuite/coinwgpu/` hold
  tests for the shared contract and backend behavior.

Rust unit tests live under `testsuite/coinwgpu/rust/unit/`. Rust shader tests
live in `testsuite/coinwgpu/rust/gouraud_shader.rs` and
`testsuite/coinwgpu/rust/shadow_shader.rs`. C++ tests remain in the matching
`testsuite` backend directories. Keep tests separate from production
implementation so each source file has one clear role.

Validation receipts excluded from this clean source layout currently resolve
to absolute paths inside the local archive at
`/mnt/Laranja/Git/externos/coin-render` (reference revision
`c28812338b8c6d51f7a1a8bd7eea6d997a291013`). The receipts are not published
remotely; publish them at a durable location and update these links before
upstream submission.

## Preparing this branch

The clean branch starts at upstream `ca247c1321bce07199ec0f034493977761b3218e`.
Its source snapshot is `codex/coin-render` at
`c28812338b8c6d51f7a1a8bd7eea6d997a291013`, including the local architecture
and integration edits present when extraction started. Production code, tests,
and examples/documentation are recorded separately. General container/thread
hardening and unrelated comment changes from the development history are
excluded; the original checkout retains them.

The transfer covers the integration source, tests, examples and tracked local
edits, rather than every historical experiment under `docs/validation`.
An additional reproducibility gap exists in the original Windows report:
`coin-render-windows-remaining-gates-20261010.md` references a final
`bgfx-device-loss.patch` that is absent from the original checkout as well as
this branch. The archive contains an earlier `bgfx-device-loss-before-history.patch`
and dependency revisions, but these must not be presented as the complete final
SDK patch. Recover that final dependency patch before claiming that the Windows
device-loss qualification can be reproduced from this branch alone.

The extraction preserves the common Core/Shell/Infra/Wiring responsibilities,
both GPU executors, legacy aliases and opt-in installation. A lexical comparison
of the moved C++ function bodies checks that moving code and formatting did not
change their tokens, allowing adjacent string literals introduced by formatting.
It does not replace compilation or behavioral tests.

Rust tests need serialized execution on this host:

```sh
cargo test --offline --locked --release \
  --manifest-path src/rendering/coinwgpu/rust_bridge/Cargo.toml \
  -- --test-threads=1
```

The default parallel library-test run passes all 42 assertions but may crash
at process shutdown. This behavior was reproduced using the untouched original
source in a separate temporary baseline. Both original and reorganized sources
pass serially; the two shader integration targets add five passing tests.
This existing teardown problem remains open.

Builds during validation run one at a time with two jobs and a per-process
virtual-memory limit of 8 GiB; documentation generation is disabled.

## Validation of this extraction (2026-10-14)

All four configurations compiled in Release with the Coin C++11 minimum:
RECORDING, RUST_BRIDGE/wgpu, BGFX, and the default Coin build with CoinRender
disabled. The installed wgpu and BGFX SDKs expose the 17 intended experimental
headers, byte-identical to source, with no private-source header leakage. A
standalone C++11 translation unit including all those headers compiled. C++
and C11 installed consumers built; Vulkan portable-sampling smokes passed on
NVIDIA with an exact sampling oracle. The default SDK excluded all 17 headers.

| Suite | Passed | Skipped | Failed |
| --- | ---: | ---: | ---: |
| recording | 136 | 54 | 0 |
| wgpu | 181 | 46 | 0 |
| bgfx | 246 | 51 | 1 |
| off | 86 | 0 | 0 |

The wgpu result combines the full Xvfb run with nine successful reruns on the
physical NVIDIA display. Those nine initially failed because CoinGL's mandatory
reference context was unavailable; `COIN_GLX_PIXMAP_DIRECT_RENDERING=1` and
NVIDIA PRIME selected the working reference without changing tolerances.
Skipped tests are not passes or platform qualification.

The BGFX failure is `CoinBgfxInstancingOpenGLTest` on NVIDIA OpenGL 615.71.09:
one pixel at (179,59), RGB error 138, when comparing affine PHONG directional
and point-light instanced/general draws. The same values were recorded before
this extraction in `/tmp/coin-render-sparse-bgfx-ctest-nvidia.log` on 2026-10-11.
The test and its tolerance remain unchanged; this driver-specific discrepancy
is still open. The Vulkan instancing test passed.

The final two declaration/implementation moves are additionally checked through
targeted composition, texture capture, builder and backend-boundary regressions.
GPU tests run serially in a systemd scope limited to 8 GiB of resident memory;
builds use two jobs and a per-process virtual-memory limit. Build peak resident
set measurements stayed below 600 MiB per process. No documentation generation
was part of these commands.
