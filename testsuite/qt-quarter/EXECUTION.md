# Execution records

## Multi-target implementation — 2026-09-27

These results supersede the singleton restriction documented in the earlier
integrated rerun below. The production backend and existing Quarter adapter
now render simultaneous native targets. No second-view GL fallback is allowed.

| Execution | Result | Evidence |
|---|---|---|
| two-viewports, BGFX/OpenGL + Vulkan, desktop AMD | 4/4 PASS | object + weighted OIT; transparent second target; independent cameras/pixels; owner destruction, survivor resize, owner recreation, second destruction |
| two-viewports, BGFX/OpenGL, Xvfb | 2/2 PASS | same cases with bounded GPU visibility wait, not hardware execution |
| two tiled FreeCAD documents, BGFX/OpenGL, desktop AMD | 2/2 PASS | object + weighted OIT; independent cameras/preselection; fresh frames in both targets after resize, maximize/restore and minimize/restore; owner destruction |
| two tiled FreeCAD documents, BGFX/Vulkan, desktop AMD | 2/2 PASS | same lifecycle; camera changed samples 35829/0, preselection 30717/0; hardware verified |
| Python result gate | 8/8 PASS | result/exit/submission policy unchanged |

Durable artifacts are `/tmp/coin-multitarget-qt-hardware-modes`,
`/tmp/coin-multitarget-qt-software-bounded`,
`/tmp/coin-multitarget-freecad-lifecycle-fixed-opengl`, and
`/tmp/coin-multitarget-freecad-lifecycle-fixed-vulkan`.
Each contains JSON, full logs and PNGs. The software weighted-OIT run initially
looked black with a fixed 180 ms capture delay. A bounded five-second wait for
the same red-pixel assertion passed without requesting another frame: CPU frame
submission is not GPU visibility. No visual threshold was relaxed.
The enlarged Qt 6.6 desktop weighted-OIT regression exposed EGL surface creation
failure when a strategy change destroyed/recreated the native swapchain. The
backend now retains that swapchain across strategy changes, and both renderers
passed the unchanged lifecycle and no-fallback checks. `wgpuFallbackReason` is
checked explicitly because earlier successful frame counters are stale after a
later fallback.
FreeCAD runs use private user/system configuration files under the artifacts;
they do not modify the user's preferences. The FreeCAD case is API-driven
preselection, not physical-pointer routing. Vulkan native presentation under
Xvfb lacks DRI3 and failed; the final native Vulkan run therefore used the real
X11 desktop, not an alleged headless pass. The initial desktop Vulkan test
exposed red/blue swapchain format inversion; the corrected backend passed the
unchanged red-geometry and independence assertions above.

## Integrated rerun — 2026-09-27

The final integrated binaries were Coin
`/tmp/coin-bgfx-evaluation-build`, BGFX
`/tmp/coin-bgfx-eval-install`, and FreeCAD
`/tmp/freecad-wgpu-build2`. The harness was rebuilt from this checkout in
`/dev/shm/coin-qt-quarter-integrated` after the final `SoNaviCube` header
change. Durable copies of the final logs, JSON results, and PNG captures are
in `/tmp/coin-qt-quarter-integrated`.

| Execution | PASS | SKIP | UNSUPPORTED | FAIL | Interpretation |
|---|---:|---:|---:|---:|---|
| Python result gate | 8 | 0 | 0 | 0 | validates malformed/missing results, exit/status disagreements, timeout, crash, SKIP and UNSUPPORTED handling |
| OpenGL lifecycle under Xvfb | 10 | 1 | 1 | 0 | maximize is SKIP because Xvfb has no EWMH WM; two-viewports is the exact single-target limitation |
| NaviCube OpenGL | 4 | 0 | 0 | 0 | object and weighted OIT, opaque and translucent, seven orientations |
| NaviCube Vulkan/RADV | 4 | 0 | 0 | 0 | same matrix on physical AMD; `--require-hardware` passed |
| depth / polygon offset / SoAnnotation, OpenGL | 3 | 0 | 0 | 0 | independent GL-oracle regressions |
| depth / polygon offset / SoAnnotation, Vulkan/RADV | 3 | 0 | 0 | 0 | physical AMD; no fallback |
| FreeCAD Face1 -> Face2 hover | 1 | 0 | 0 | 0 | distinct captures; 2930 changed samples |

Lifecycle PASS cells are first expose, coalescing, idle, resize,
minimize/restore, bottom panel, DPR at 1x and 2x, viewport recreation, and
wheel rotation. Maximize is not claimed by the Xvfb run: it exits 77 with
`no EWMH window manager`. The independently executed real-desktop integration
smoke passed maximize and the remaining lifecycle/window-state stages on both
BGFX/OpenGL and Vulkan. The two-viewports case exits 78 with the exact BGFX
singleton diagnostic after verifying that the owning viewport keeps its
pixels and accepts another frame; it is not silently counted as success.

The NaviCube matrix retains fourteen labelled/unlabelled PNG pairs per case:
six axis-aligned orientations and one oblique rotation around `(1,1,0)` by
`0.65` radians. All eight backend/mode/alpha cases pass the RGB MAE and label
mask assertions. The largest translucent MAE is `0.4644` on BGFX/OpenGL and
`0.5833` on Vulkan. In the oblique translucent case, the GL label mask has
1667 pixels; BGFX/OpenGL has 1522 with 147 disagreements and Vulkan has 1523
with 146 disagreements, both within the fixed threshold. The GL side uses raw
`glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE)` so QImage premultiplication cannot
silently unpremultiply translucent oracle pixels.

Representative commands:

```sh
cmake --build /dev/shm/coin-qt-quarter-integrated --parallel 4
python3 -m unittest discover -s testsuite/qt-quarter -p test_runner.py

LD_LIBRARY_PATH=/tmp/coin-bgfx-evaluation-build/lib:/tmp/coin-bgfx-eval-install/lib:/tmp/freecad-wgpu-build2/lib \
xvfb-run -a -s '-screen 0 1280x1024x24' \
  python3 testsuite/qt-quarter/run.py \
  --harness /dev/shm/coin-qt-quarter-integrated/QtQuarterRegression \
  --renderer opengl --case navicube \
  --artifacts /dev/shm/coin-qt-quarter-integrated/final2-navicube-opengl

VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
LD_LIBRARY_PATH=/tmp/coin-bgfx-evaluation-build/lib:/tmp/coin-bgfx-eval-install/lib:/tmp/freecad-wgpu-build2/lib \
xvfb-run -a -s '-screen 0 1280x1024x24' \
  python3 testsuite/qt-quarter/run.py \
  --harness /dev/shm/coin-qt-quarter-integrated/QtQuarterRegression \
  --renderer vulkan --require-hardware --case navicube \
  --artifacts /dev/shm/coin-qt-quarter-integrated/final2-navicube-vulkan
```

The four viewer-private overlay cells remain explicit UNSUPPORTED in the
standalone harness: `foregroundroot`, `decorationroot`, `axis-cross`, and
`rubber-band`; the latter two also carry `expected_failure=true`. They were
not included in the PASS totals above and are not presented as implemented.
Intel, NVIDIA, Direct3D, Metal, and real mixed-DPR monitor migration remain
unverified.

## Initial execution — 2026-09-26

Test-only worktree based on Coin `932b5a859f`. Tested against the existing
integration sources `/mnt/Laranja/Git/externos/coin` and
`/mnt/Laranja/Git/externos/freecad-source`, Coin build
`/tmp/coin-bgfx-evaluation-build`, BGFX installation
`/tmp/coin-bgfx-eval-install`, and FreeCAD build `/tmp/freecad-wgpu-build2`.
Those integration trees contain external functional work; this commit does
not reproduce or modify it. Results characterize these supplied binaries,
not the unmodified Coin base commit. The harness build is
`/tmp/coin-qt-quarter-tests`.

| Execution | Result | GPU evidence / interpretation |
|---|---|---|
| CPU result gate | 7 PASS | missing submission, malformed result, crash, skips, unsupported and timeout tested |
| Xvfb/OpenGL lifecycle, final fresh-frame assertions | 6 PASS, 5 FAIL, 1 UNSUPPORTED | software GL; no physical GPU claim |
| FreeCAD Face1 → Face2 | PASS | actual Quarter viewport crop changes; BGFX/OpenGL submission, software Xvfb |
| NaviCube opaque, both transparency modes | PASS on Vulkan and BGFX/OpenGL | six orientations plus labelled/unlabelled image pairs; AMD physical GPU for both backends |
| NaviCube translucent, both transparency modes | FAIL on both backends | RGB composition differs from independent GL oracle; label masks also saved |
| depth functions/test/write | FAIL on both backends | overlapping geometry; ten variants; no skip/fallback |
| polygon offset | FAIL on both backends | coincident geometry with offset disabled/enabled |
| SoAnnotation | PASS on both backends | behind-geometry annotation compared with GL |
| two simultaneous viewports | UNSUPPORTED | exact singleton rejection; owner retains pixels and accepts a subsequent frame |
| foregroundroot / decorationroot | UNSUPPORTED | viewer-private wiring not available in minimal Quarter; not claimed covered |
| axis cross / rubber-band | UNSUPPORTED, expected_failure | not claimed ported or rendered |
| Vulkan native lifecycle under Xvfb | FAIL | physical AMD probe succeeds, window target reports missing required capabilities; offscreen Vulkan still executes |
| no DISPLAY | SKIP, exit 77 | native surface genuinely cannot execute; aggregate is not success |

The six passing final lifecycle cells are first-expose, coalescing, idle,
minimize/restore, recreate and wheel/rotation. Resize, maximize/restore,
bottom panel, and DPR notification at 1x/2x fail the **fresh frame** assertion.
Earlier pixel-only checks passed these cases, demonstrating why detecting old
geometry alone was insufficient. These failures are left visible for the
functional agents; this test branch does not repair them.

Xvfb has no desktop window manager in this run: maximize results require a
second run under a real WM before attributing the failure to the renderer.
Real monitor-to-monitor DPR migration remains unexecuted. A first-expose run
on the existing physical desktop also failed its geometry capture; desktop
occlusion versus a presentation bug was not conclusively isolated.

The system Qt and user PySide6 initially had an ABI mismatch, which the runner
recorded as FAIL, not SKIP. The real hover probe was subsequently executed with
the already installed compatible Qt libraries:

```sh
LD_LIBRARY_PATH=/home/dikluwe/.local/lib/python3.12/site-packages/PySide6/Qt/lib:/tmp/coin-bgfx-evaluation-build/lib:/tmp/coin-bgfx-eval-install/lib:/tmp/freecad-wgpu-build2/lib \
xvfb-run -a python3 testsuite/qt-quarter/run.py \
  --harness /tmp/coin-qt-quarter-tests/QtQuarterRegression \
  --freecad /tmp/freecad-wgpu-build2/bin/FreeCAD \
  --renderer opengl --case hover --artifacts /tmp/coin-qt-hover-verified
```

Other commands follow README.md. Physical Vulkan used
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json` and
`--require-hardware`; physical OpenGL used the existing X11 session and the
same hardware requirement. Both physical visual runs report 3 PASS / 4 FAIL.

Logs, results and images are retained outside the commit in
`testsuite/qt-quarter/artifacts/` in this worktree. The directories distinguish
final lifecycle, visual software, physical Vulkan/OpenGL, hover and no-display
runs. Do not combine older and newer lifecycle outcomes as if they used the
same assertions. Shader/GPU bugs are not certified fixed merely because a
test case exists. AMD/RADV and AMD/radeonsi were exercised; Intel, NVIDIA,
Direct3D, Metal, real mixed-DPR monitors and full viewer overlay wiring remain
unverified.
