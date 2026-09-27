# Native Qt / Quarter / FreeCAD regressions

This is a standalone **test-only** project. It links the real patched Quarter
and NaviCube from FreeCADGui and the real Coin BGFX backend. It does not copy
the presenter, frame queue, selection implementation or backend. Source and
build trees must match each other (including Qt major version and Coin ABI).
The test commit can therefore be kept separate from integration changes.

## Build and run

Requirements: Qt 6 Widgets/OpenGLWidgets/Test development packages, X11,
Python 3, Xvfb, `glxinfo`, `vulkaninfo`, and existing WGPU-enabled Coin and
FreeCAD builds. No Python image library is required.

```sh
cmake -S testsuite/qt-quarter -B build/qt-quarter -G Ninja \
  -DCOIN_SOURCE_DIR=/absolute/coin-source \
  -DCOIN_BUILD_DIR=/absolute/coin-build \
  -DFREECAD_SOURCE_DIR=/absolute/freecad-source \
  -DFREECAD_BUILD_DIR=/absolute/freecad-build
cmake --build build/qt-quarter
export LD_LIBRARY_PATH=/absolute/coin-build/lib:/absolute/bgfx-install/lib:/absolute/freecad-build/lib
python3 -m unittest discover -s testsuite/qt-quarter -p test_runner.py
xvfb-run -a -s '-screen 0 1280x1024x24' \
  python3 testsuite/qt-quarter/run.py \
  --harness build/qt-quarter/QtQuarterRegression \
  --freecad /absolute/freecad-build/bin/FreeCAD --artifacts build/qt-quarter/xvfb
```

For physical GPUs, use an unobscured X11 desktop, an explicit Vulkan ICD when
several GPUs are installed, and `--require-hardware`:

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json \
python3 testsuite/qt-quarter/run.py \
  --harness build/qt-quarter/QtQuarterRegression \
  --freecad /absolute/freecad-build/bin/FreeCAD \
  --renderer vulkan --require-hardware --artifacts build/qt-quarter/radv
```

Run `--renderer opengl` separately for radeonsi. Repeat on Intel and NVIDIA.
Native lifecycle screenshots read the actual composed X11 surface, not a
QWidget paint approximation. Another window covering the viewport invalidates
this measurement; use a dedicated X server/session. Xvfb generally exercises
Mesa software GL; it is **not** evidence of hardware presentation. Vulkan
may still select hardware under Xvfb; the per-case adapter is recorded rather
than inferred from the presence of an installed driver. Windows/D3D and Metal
require a platform-specific native capture harness; this X11 harness does not
claim those platforms are tested.

`--case` and `--renderer` can be repeated. `--case hover --freecad ...` executes
the actual FreeCAD Face1 → Face2 preselection API and verifies displayed pixel
changes. It is an API-driven hover regression, not physical pointer hit-testing.
The initial DPR is tested at 1x and 2x; the intra-process test exercises the Qt
DPR notification. Moving a window between real differently scaled monitors
still requires a physical multi-monitor test.

## Results: no silent fallback or skip

Each case uses a new process, with a hard timeout. Exit codes are 0 PASS,
77 SKIP, 78 UNSUPPORTED and 1 FAIL. The aggregate returns FAIL first, then
SKIP, then UNSUPPORTED; a partly unexecuted matrix is never an all-green exit.
CTest displays code 77 explicitly as skipped and code 78 as nonpassing.
SKIP is emitted before launching Qt when DISPLAY is absent. GPU setup/render
errors are conservatively FAIL, not converted into alleged GPU absence.
A capability query alone is never sufficient to pass or to skip rendering.
PASS must include a structured result, matching exit status, a BGFX submission
trace and the case's pixel/state assertions. Crashes, missing results, wrong
executable/library, fallback and timeouts fail. `--require-hardware` also fails
a PASS whose selected adapter cannot be identified as physical.

Artifacts contain inventory.json, aggregate results.json, per-case result.json,
process.log and PNGs. All captures (not only mismatches) are retained to permit
review. Failure-screen.png is saved on caught harness errors; a process crash
may prevent a final screenshot, but its log, exit and prior images remain.
CI uploads artifacts even when the matrix fails. Do not use `|| true` as a
success policy or silently discard UNSUPPORTED cells.

## Coverage and oracle

| Case | Automated assertion | Limitation / expected result |
|---|---|---|
| first-expose | native child visible, production redraw, red geometry in screen capture without forced request | dedicated/unobscured X11 |
| frame-coalescing / idle | 100 requests bounded to ≤3 redraws; stable viewport schedules no further redraw | real GPU submission checked by runner |
| resize / panel | 24 resizes; bottom dock open/close; native size tracks viewport and geometry remains | panel simulates Report/Python dock layout |
| maximize / minimize | window states and restored native pixels | X11 state handling; real WM run recommended |
| dpr | initial actual Qt scale 1x/2x plus notification redraw | not a real monitor migration |
| recreate | destroy/recreate production Quarter viewport; valid pixels | process remains alive |
| wheel-rotation | actual Qt input to Quarter; displayed pixels must change | default examiner navigation; no synthetic camera edit |
| hover | real FreeCAD Face1 → Face2 preselection and displayed change | `--freecad` mandatory; no UI hit-test |
| two-viewports | second must render or exact singleton diagnostic; first retains pixels | explicit UNSUPPORTED while single-target; crash/other errors FAIL |
| NaviCube | six orientations; production GL vs WGPU RGB MAE <6 and label-contribution mask disagreement <35% | labelled/unlabelled pairs isolate text, including translucent labels; front text/rear occlusion evaluated against independent GL traversal |
| NaviCube modes | object + weighted_oit × opaque + translucent; Vulkan + BGFX/OpenGL | all four combinations, all six PNG pairs retained |
| depth | overlapping near/far cubes; all eight functions, test off and write off; GL MAE <3 | unsupported implementation fails, never skipped |
| polygon-offset | coincident differently colored geometry with offset off/on; GL MAE <3 | missing support is a recorded FAIL pending explicit capability contract |
| SoAnnotation | green geometry behind red cube inside annotation; independent GL ordering oracle | actual node traversal |
| foregroundroot / decorationroot | explicit UNSUPPORTED | private FreeCAD viewer wiring not exposed by Quarter; needs full viewer instrumentation |
| axis cross / rubber-band | explicit UNSUPPORTED with expected_failure=true | legacy viewer drawing not exported as WGPU nodes; not falsely claimed as rendered |

The NaviCube oracle renders the actual FreeCAD SoNaviCube, using fixed viewport,
size, font, label textures and axis-aligned orientations. It compares two
independent production actions, not WGPU-generated goldens. A missing GL front
label makes the test fail, preventing blank-vs-blank success. Font/GL-driver
differences are bounded by the recorded tolerances, not auto-updated baselines.
Keep DejaVu Sans installed in the pinned CI image. GL and WGPU may share scene
construction bugs; this comparison does not replace reviewing PNG artifacts.

## CI

The manual workflow uses a dedicated self-hosted `freecad-wgpu` GPU runner.
Repository variables COIN_SOURCE_DIR, COIN_BUILD_DIR, FREECAD_SOURCE_DIR,
FREECAD_BUILD_DIR and BGFX_INSTALL_DIR point to matched integration builds.
It runs software Xvfb and a hardware-required X11 matrix independently,
uploads both even on failure, and fails the overall job if any matrix contains
FAIL, SKIP or UNSUPPORTED. Known incomplete overlays are intentionally visible
nonpassing cells, not blanket allowances for future regressions.
