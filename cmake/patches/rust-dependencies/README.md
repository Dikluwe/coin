# External Rust dependency sources and preserved changes

CoinRender no longer carries full Rust dependency copies in its source tree.
Cargo uses `[patch.crates-io]` to select four corrected packages from
[Dikluwe/coinrender-rust-deps](https://github.com/Dikluwe/coinrender-rust-deps),
all pinned to commit
[`b39b068fe5aed4782790075866bee8d010bed3ec`](https://github.com/Dikluwe/coinrender-rust-deps/commit/b39b068fe5aed4782790075866bee8d010bed3ec).
The other registry dependencies, including wgpu 24, remain selected by Cargo.lock.

The external commit contains the same 263 dependency source/license/manifest
files previously used by CoinRender, without changes to their bytes. This is a
source-location change, not a dependency upgrade or a claim that all changes are
upstream-accepted bug fixes. Original licenses remain in the external repository.

## Versions and changes

| Package | Preserved local changes | Compact patch |
| --- | --- | --- |
| Naga 24.0.0 | GLSL lowering for multisampled depth loads, including scalar depth extraction and the selected bounds policy | [naga-24.0.0.patch](naga-24.0.0.patch) |
| wgpu-hal 24.0.4 | GLES sample-mask and multisample attachment handling, owning-device VAO restoration, and Vulkan fence/swapchain/X11 lifecycle paths | [wgpu-hal-24.0.4.patch](wgpu-hal-24.0.4.patch) |
| wgpu-core 24.0.5 | Thread-local tracking/snatch-lock teardown paths | [wgpu-core-24.0.5.patch](wgpu-core-24.0.5.patch) |
| glow 0.16.0 | `sample_mask_i` interface and implementations for the GLES path; the WebGL method remains a no-op because WebGL exposes no exact sample mask | [glow-0.16.0.patch](glow-0.16.0.patch) |

The patches contain the full differences in 16 source files (450 added and 104
removed lines), including the added
`src/vulkan/x11_wsi.rs`. Their applicability is to the exact package versions
above, with the published normalized Cargo manifests. They are not patches
against an arbitrary current wgpu workspace checkout.

The external repository records each published crate's original upstream Git
revision in `UPSTREAMS.json`. Its first commit is the published-source baseline;
the next commit applies these corrections/adaptations. This distinguishes the
upstream source from the local changes. Existing platform and lifecycle limits
remain open; relocating the changes does not qualify additional GPUs or APIs.

## Fetching and offline builds

A first build needs access to crates.io and the external Git repository. Fetch
once using the checked-in manifest and lockfile:

```sh
cargo fetch --locked --manifest-path src/rendering/coinwgpu/rust_bridge/Cargo.toml
```

After the required crates and the selected Git commit are cached, build offline:

```sh
cargo build --offline --locked --manifest-path src/rendering/coinwgpu/rust_bridge/Cargo.toml
python3 testsuite/coinwgpu/test_external_dependencies.py --source-root .
```

The source checks require Git and Cargo. They check the resolved package versions,
full Git revisions and the reverse applicability of all four compact patches.
They do not modify cached dependency sources or initialize a GPU. CMake registers
these checks only for a wgpu test build; RECORDING does not acquire a Rust
requirement.

CMake tracks Cargo.toml and Cargo.lock. The corrected dependencies live in Cargo's
Git cache rather than in `rust_bridge/vendor`. Do not edit that cache to update
the product: use a new reviewed external commit and change the manifest/lockfile
explicitly.

## Updating or retiring a change

Review the compact patches and the external source diff, update the full `rev`
for all affected package overrides, regenerate Cargo.lock without unrelated
version changes, and rerun source, bridge and rendering tests. When an upstream
release supplies a change, evaluate that release explicitly before removing its
override. Using the unmodified registry packages today would drop the changes
listed above.

Historical Coin commits still contain the earlier vendored snapshots. No Git
history was rewritten; the current source tree and contribution diff exclude
those complete crate copies.

## Separation validation

The source-location change was checked on the Linux host with the new Cargo Git
cache: all 263 fetched files matched the previous Coin snapshots byte-for-byte,
and applying the four compact patches to the published-source baseline reproduced
those same files. `cargo fetch --locked` and subsequent offline resolution passed;
Cargo.lock changed only the four package source entries, without version changes.

The production CoinRender CMake build, production/test-hook Rust static libraries,
71 Rust unit tests (four ignored), five shader integration tests, seven private
bridge ABI/build checks and the new external-source CTest passed. The additional
physical-GPU bump integration test remained ignored. These focused checks do not
qualify Windows DirectX, Vulkan or OpenGL, or close the documented lifecycle limits.
