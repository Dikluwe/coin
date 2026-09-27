# Execution record — 2026-09-26

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
