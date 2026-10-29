# Retained anisotropy and global VNDF IBL — 2026-10-29

Local qualification of F12/F13/C17 on `codex/coinrender-retained-integration`.
Source PBR revision: `ce72805f0536f8f0ef07dc5575d301b3ddffa507`.
Retained fork baseline: `17a9e4b21b643cb44d42e103183d8384eef6a6a5`.
The changes are implemented/tested locally; no upstream-ready patch series has
been extracted or validated against the fork's native consumers.

## What is connected

- Native direction-coordinate demand even without images; per-corner UV indices,
  independent direction/normal transforms and coordinate provenance that survives
  lobe disable/enable. The existing native updater resolves strength/rotation
  on state paths, stopping before shapes; the native planner stays authoritative.
- Shared MikkTSpace lowering after the one capture, with sparse surface attributes.
  Callback controls choose their normal primitive generator when an older direct
  geometry shortcut cannot represent tangent UVs. No second capture or planner.
- Authored strength/rotation in BGFX direct lighting; global environment VNDF
  integration from pinned PBR, default 16 source samples with a bound of 128.
  Zero explicitly selects the older bent-reflection diagnostic profile.
- Optional renderer-independent albedo preparation/read/write/evaluation,
  immutable CPU snapshots, SOURCE/prefiltered cube-array layers and packed LUTs.
  CPU budget is explicit for the albedo product/preparation; GPU budget charges
  allocated mip chains/padding and concurrent generations. Capability/budget
  refusal preserves publication and never silently substitutes an approximation.
- FreeCAD per-link `anisotropy` / `anisotropyRotation` overrides and optional ANIS
  protocol block, with image-free planar tangent authoring. Old packets remain valid.

## Results

`surface-comparison.json`: **14 fixtures, all 28 color/depth buffers exact**
between retained, callback and the separately built pinned PBR SDK. Cases include
no images, IBL-only, direct-only, 0/16/64 samples, mirrored UV/model transforms,
degenerate UVs, separate bump transform, image-free unit 7 projection, sphere and
nonuniform model. The common public consumer is compiled against each SDK;
the pinned consumer loads the pinned libraries, not this branch's libraries.

**18 update buffers exact** between retained/callback. Strength, rotation,
normal scale, zero/restore and translation retain captures=plans=transports=1
and unchanged copied vertices. Invalid strength/angle, sample count >128 and
CPU/GPU budget failures preserve color/serial; retry reproduces the prior image.
Starting with no captured anisotropy dependency and then enabling it recaptures
exactly once, with identical resulting pixels. See the runner and C++ test for
these explicit assertions; the final CTest run includes that additional case.

`albedo.csv`/`albedo.log`: imported CPU suite, **81 independent NDF quadrature
cases**, max A/B error 0.000149786472 for direct VNDF quadrature and maximum
prepared-product interpolation error 0.00328850746. The earlier effective
isotropic energy approximation has worst error 0.43926093 in this campaign.
Serialization, content mutation, dimensions, budgets and rollback also pass.
These are numerical energy/albedo errors, not rendered-image error or frame-time gains.

`tests.xml`: **18/18 regressions pass**, covering prior PBR/IBL/surfaces/effects,
native capture, connector/no-borrow, BGFX Core/instancing and the new controls.
`native-cache.json`: **15/15 native cache/admission/retirement checks** with
anisotropic resources active, including resize, static reuse and failed retry.
The contribution auditor classifies 196 changed non-documentation files and
checks 64 native files for executor dependencies.

## Actual AssemblyExample

`overview/` and `detail/` are two actual FreeCAD native viewport campaigns,
each with **nine GUI stages and 20 exact independent offscreen color/depth
buffers**. Each campaign fixes the scene, camera, lighting/exposure and output
size (1234×886) across strength/rotation/isotropic comparisons. Only the direction
changes between `anisotropic.png` and `rotated90.png`. Exposed steel receives
strength .85, roughness .32, metallic 1; paint keeps the earlier appearance preset.
No map pattern or fabricated image is used. CAD material data is not saved.

Each campaign tests static reuse, 90° rotation, weaker anisotropy, zero/restore,
selection, movement and restoration. Through material changes, selection and
movement: **one capture, one plan, one transport, 202788 copied vertices**.
Combined selection/placement restoration conservatively recaptures once;
cumulative counters become two and copied vertices 405576. Restored native
pixels exactly match the original. Forced static repaints do not repeat shading.
The worker reports **zero pixel readbacks and zero Qt image bytes** throughout.
OS snapshots are test evidence only, never a production display transport.

Native screenshots are compared separately with offscreen controls; sparse
raster/edge differences remain and per-stage errors are preserved in each
`comparison.json`. They are not claimed byte-identical to offscreen output.
The PNGs are untouched native OS surface snapshots. The final detail campaign
uses the current FreeCAD placement API, avoiding a deprecation notification
that overlapped an earlier, discarded preview.

One retained albedo product: **1050624 CPU bytes**. Complete environment GPU
allocation in this campaign: **3418080 bytes**. These exclude other CPU/GPU
geometry, textures, render targets, driver allocations and the host process.
Default preparation took about **4.1 s** in the CPU control. A precomputed `.cani`
can be supplied through `CoinRenderOptions::pbrAnisotropyProducts`; scalar edits
reuse the product and the single environment upload. This campaign makes no
whole-process memory reduction or Android/wgpu performance claim.

Environment: Linux X11/Xvfb, NVIDIA GeForce RTX 3060 Laptop GPU, Vulkan driver
615.71.09; locally built FreeCAD 26.3 with PySide6 Qt. The host uses its bundled
Coin and the isolated worker this branch's Coin. No C++ Coin ABI object crosses
the process boundary. Startup has the pre-existing AddonManager `defusedxml`
warning; qualification otherwise completes. Binary/input hashes are in
`provenance.json`; source hashes/classification are in `boundary-audit.json`.

## Reproduction and limits

See `commands.txt`. The compressed scenes and exact worker requests/receipts
preserve the input/camera state; raw buffers remain in the listed `/tmp`
directories. Results/hashes and selected PNGs are archived here. The SDK
comparison uses `CoinRenderPbrSurfaceReference.cpp` with the pinned checkout's
`experimental/include`, public/generated includes, pinned libraries and rpath.

FreeCAD authoring example:

```python
renderer.material_overrides.setdefault("Bucket", {}).update(
    metallic=1., roughness=.32, anisotropy=.85, anisotropyRotation=.3)
```

This is bounded interactive **global** IBL, using the source branch's VNDF
ratio estimator. Local/planar reflections/probes, stationary convergence,
geometry AO, area lights, skybox/dynamic environment, transparent/mixed PBR,
wgpu/native GL PBR and persistent FreeCAD material editing remain outside this
connection. An initially isotropic scene without directional provenance needs
one capture/plan to enable the lobe; role/UV and mixed dependency changes still
use conservative invalidation. The explicit CPU product module is candidate
F13; shader/resources/protocol stay downstream C17.
