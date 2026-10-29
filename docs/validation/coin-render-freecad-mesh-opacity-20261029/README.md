# Curved link opacity: missing CAD tessellation face — 2026-10-29

C18 fixes the visibly missing wall on **BucketLink2**, the curved silver link
beside the bucket cylinder. The source AssemblyExample BRep is valid/closed with
one solid and volume 17079.26. `Shape.tessellate(.2)` generated an open mesh:
one planar face (index 3, area about 916.15) had no triangles and the complete
export had **217 boundary edges**. This was already present in the C17 preview;
agreement between renderers consuming the same incomplete mesh did not detect it.
The final shader/target alpha is opaque; changing material alpha was not the fix.

## Correction

`freecad_mesh_export.py` validates finite vertices/indices, directed manifold
edge incidence, degeneracies and positive volume for meshes of closed solids.
Healthy ordinary tessellations stay on their existing path. Only incomplete
results are retried with `MeshPart.meshFromShape`, keeping linear deflection .2
and explicit angular deflections .2 then .3 on fresh copies. The first candidate
still has 20 boundary edges and is refused. The accepted .3-radian candidate
has **1716 triangles, zero boundary/inconsistent/nonmanifold edges**, and volume
17038.17: about **0.24%** below the CAD volume. The preview independently requires
this part's volume to be within 1% of the CAD solid. The original open mesh's
signed-volume diagnostic is origin-dependent and is not a physical solid volume.

If no candidate passes, attachment fails before viewport/UI takeover. CAD
topology, placement, material opacity, face culling, shaders and the native
capture/planner are unchanged. Attempts belong to the one-time document import;
no per-frame tessellation, new Coin traversal or new planning is introduced.
`mesh-reports.json` records every candidate rather than hiding retries.

## Validation

- Six independent Python unit cases cover a known closed tetrahedron/volume,
  translated coordinates, missing/reversed/duplicate faces, malformed data,
  unchanged healthy meshes, candidate refusal, recovery and source preservation.
  The registered CTest passes (`tests.xml`, `unit.log`).
- The actual native FreeCAD viewport passes **nine GUI stages**, including
  direction/strength changes, isotropic/restore, selection and movement of
  **BucketLink2**, restoration and forced static reuse. The selected request
  explicitly addresses part index 5 (`solid-check.json`).
- **20 independent offscreen retained/callback color/depth buffers are exact**.
  Native OS screenshots retain the previously documented sparse raster-edge
  differences versus offscreen; see `comparison.json` for the actual errors.
- The repaired link's depth is now nearer in 17386 pixels of the previously
  open region (ROI and threshold are recorded). The before/after camera is exact.
- Through material edits, selection and movement: **1 capture, 1 plan, 1
  transport**; all stages retain **13 exported meshes / 15 tessellation attempts**.
  Combined selection/placement restoration conservatively recaptures once, as
  before. Restoration reproduces the original corrected native pixels.
- All native requests retain zero pixel readbacks/Qt image bytes. Back-face
  culling remains enabled and no old capture fallback runs.

`before.png` is the original C17 native snapshot. `after.png` is the corrected
native snapshot with identical camera/light/material direction. Both are
untouched renderer outputs. `after-freecad.png`, `selected.png` and `moved.png`
show the real viewport. The FCStd is never saved.

Environment: local FreeCAD 26.3/PySide6 Qt, X11/Xvfb, NVIDIA RTX 3060 Laptop GPU,
Vulkan driver 615.71.09. The pre-existing AddonManager `defusedxml` startup warning
is unrelated. Worker/library/CAD hashes are in `provenance.json`; shader/executor
binaries are the same C17 binaries. The boundary audit classifies 198 files and
checks 64 native files; C18 is exclusively application ingress and tests.

## Reproduce

Run `python3 testsuite/coinrender/test_freecad_mesh_export.py` or the CTest
`CoinRenderFreeCADMeshExportTest`. The viewport uses the C17 preview macro with
`COIN_ASSEMBLY_ANISOTROPY_PREVIEW=1`, `COIN_ASSEMBLY_ANISOTROPY_DETAIL=1`, and
`COIN_ASSEMBLY_PREVIEW_INTERACTION_PART=BucketLink2`; use the C17 `commands.txt`
with a fresh output directory (qualified output: `/tmp/coin-opacity-final`).
Then run `compare_freecad_pbr_effects.py` against that output. `requests.txt`,
`frames.jsonl`, `scene.iv.gz` and mesh reports preserve the exact inputs/camera.
Raw buffers remain in `/tmp/coin-opacity-final`; archived results/hashes and
selected PNGs remain here. MeshPart is required only to repair invalid ingress
meshes. This fixes the tested valid solid; it is not a general CAD healing tool.
