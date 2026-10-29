# Retained PBR maps and scalar material updates — 2026-10-29

This receipt qualifies F11/C16 on `codex/coinrender-retained-integration`.
The source PBR branch is pinned at `ce72805f0536f8f0ef07dc5575d301b3ddffa507`;
the retained fork baseline is `17a9e4b21b643cb44d42e103183d8384eef6a6a5`.
These are local integration results, not upstream acceptance or a performance
claim for Android, wgpu or the fork's GL executor.

## Connected

- Native logical stored image roles and per-unit UV capture (0–7); immutable
  native descriptors, one native updater and one native planner.
- Base/emissive sRGB, linear ORM, RGB/RGBA normal and L/LA height sources;
  wrapped/clamped height conversion, role-aware mips and MikkTSpace corners.
- Sparse normal/tangent attributes keep the base vertex stride unchanged.
  Tangent-expanded mapped geometry can cost additional memory.
- BGFX/Vulkan opaque PBR bindings, isotropic IBL and existing shadows/SSAO.
- Incremental scalar factors, including normal scale/Y flip and authored emission,
  without recapture/planning/transport reconstruction on qualified state paths.
- Optional map files/projection per FreeCAD link and scalar worker packets.
  Selection/hover combine with authored emission; native presentation remains
  free of Qt image copies and production pixel readbacks.

## Controls and observations

`surface-comparison.json`: 14 fixtures, 28 exact retained/callback buffers and
20 exact update buffers. The final source SDK control also has all 28 buffers exact. An earlier run
(`surface-comparison-earlier.json`) had one 8-bit channel differing by 1 in the
nonuniform sphere, with exact depth. The runner retains a narrowly bounded
allowance for that case (at most one channel by 1); it was unused in the final
run. The shader resolves per-vertex tangent frames instead of the pinned SDK's
CPU resolution, so these measurements are not a universal bit-equivalence
guarantee. No other fixture has a tolerated difference.

The fixtures cover color/ORM/emission with zero image alpha, normal and height,
neutral/disabled normals, reflection/nonuniform model transforms, mirrored and
degenerate UVs, default cube/sphere UVs, generated planar coordinates, a
normal-only projection and image unit 7. Update controls also reject invalid
normal scale and missing image roles without replacing the published image;
image mutation recaptures, while scalar edits and translation retain capture.
Legacy bump semantics remain refused explicitly.

`assembly-preview.json`, `assembly-comparison.json` and `factor-checks.json`:
actual FreeCAD AssemblyExample native viewport at 1234×886, 13 link meshes plus
studio floor, 174381 copied vertices. Nine previews cover effects, detail,
selection and movement. The roughness/normal-scale edit and restoration both
keep **captures=1, plans=1, transport_builds=1**, with unchanged copied vertices.
Restored pixels equal the original. Twelve recorded requests independently
replay to 24 exact retained/callback offscreen buffers. The mapped native screenshots are not byte-identical to the offscreen controls:
there are sparse raster/edge differences (recorded maxima up to 235 levels),
under the previously documented native/offscreen limits. See the per-stage
mean errors and maxima in `assembly-comparison.json`; do not interpret this
campaign as full native/offscreen pixel equivalence.

`assembly-maps.png` and `assembly-maps-freecad.png` show the actual native
output. Boom receives color/ORM/normal and Bucket a height source. These small
16×16 images are deliberately conspicuous numerical patterns, **not finished
materials or a proposed production appearance**. The comparison uses the same
camera and transforms for both captures.

`native-regression.json`: 19 actual GUI stages, 19 exact presented native
images and 40 exact independent offscreen buffers. This also covers existing
packet compatibility, selection/highlight, motion, visibility, camera/resize,
document lifecycle and restoring the ordinary FreeCAD viewport.

`native-cache.json`: all 15 static reuse/admission/failure-retry/retirement checks
also pass with the mapped scene and optional material packet.

`tests.xml`/`tests.log`: all 15 selected native PBR, IBL, surface, effect,
connector/no-borrow, BGFX Core and instancing regression tests pass. The final
build log is for the affected targets; an earlier broad all-target build was
interrupted after compiling additional unrelated test targets.

Environment: Linux X11/Xvfb; NVIDIA GeForce RTX 3060 Laptop GPU; Vulkan,
driver 615.71.09. GUI uses the locally built FreeCAD and PySide6 Qt.
The host retains its own bundled Coin; the worker loads this branch's Coin.
No Coin C++ ABI object crosses the process boundary. Startup logs contain the
pre-existing AddonManager `defusedxml` warning, unrelated to viewport tests.
`provenance.json` records executable/library/input hashes. The boundary audit
classifies 187 files and checks 58 native files for executor dependencies.
Candidates still require separate extraction and native fork validation.

## Reproduction

Build and run the affected CMake targets/CTest tests as listed in `commands.txt`.
The pinned SDK control compiles `CoinRenderPbrSurfaceReference.cpp` using the
pinned checkout's `experimental/include`, `include`, generated build includes
and its `libCoinRender`/`libCoin`, with an rpath to those libraries.

For the mapped Assembly preview, set `COIN_ASSEMBLY_SURFACE_MAPS` to a JSON
file with absolute image paths, as in `maps/maps.json`. Rebase those paths when
moving the checkout. The compressed `.iv` scene preserves the original receipt's
absolute paths. `assembly-requests.txt` and `assembly-frames.jsonl` are the exact
worker input/output; camera values are preserved there. Large raw buffers are
kept in the `/tmp` output directories referenced by `commands.txt`; archived
checksums/comparison results and selected PNGs remain in this repository.

## Remaining

Already present in parts of `pbr-clean`, awaiting retained coupling:
anisotropy and its IBL, planar/rough local reflections and probes, BVH geometry
AO, stationary refinement, area lights/LTC and penumbra passes, skybox/dynamic
environment. Existing bounded SSAO/directional PCF stay connected.

Further implementation/qualification: wgpu and native Coin GL PBR, complete
transparent/glass and mixed legacy/PBR composition, MSAA/fog/stereo/layers,
FreeCAD document material import/editor/persistence, live image replacement,
separate explicit bump-coordinate sources and producer/cube/volume maps.
Resource/UV edits and some mixed notifications still recapture; tangent meshes
rebuild surface transport for geometry changes. Finer shadow/AO dependency
updates and tangent memory reductions remain opportunities. No total-memory
or frame-time improvement is claimed by this campaign.
