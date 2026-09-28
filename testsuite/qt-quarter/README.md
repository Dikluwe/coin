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
`--case freecad-multi --freecad ...` tiles two actual FreeCAD documents,
checks independent cameras and preselection, and closes the original viewport
while the survivor continues rendering. It also resizes, maximizes/restores and
minimizes/restores, requiring fresh frames for both targets. Both multi-viewport
cases exercise object and weighted OIT; a GL fallback in either view is forbidden.
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
| first-expose | native child visible, production `wgpuFrameCount` advances, red geometry in screen capture without forced request | dedicated/unobscured X11 |
| frame-coalescing / idle | 100 requests bounded to ≤3 completed WGPU frames; stable viewport completes no further frames | real GPU submission checked by runner |
| resize / panel | 24 resizes; bottom dock open/close; native size tracks viewport and geometry remains | panel simulates Report/Python dock layout |
| maximize / minimize | window states and restored native pixels | X11 state handling; real WM run recommended |
| dpr | initial actual Qt scale 1x/2x plus notification redraw | not a real monitor migration |
| recreate | destroy/recreate production Quarter viewport; valid pixels | process remains alive |
| wheel-rotation | actual Qt input to Quarter; displayed pixels must change | default examiner navigation; no synthetic camera edit |
| hover | real FreeCAD Face1 → Face2 preselection and displayed change | `--freecad` mandatory; no UI hit-test |
| two-viewports | two distinct native BGFX surfaces; independent cameras; transparent second target; destroy initialization owner, resize survivor, reopen first and destroy second | object and weighted OIT; bounded GPU visibility wait without a forced frame; GL fallback, singleton rejection, corruption and crash all FAIL |
| freecad-multi | two tiled FreeCAD documents; independent cameras/preselection; fresh frames in both after resize/maximize/minimize; owner destruction leaves rendered survivor | object and weighted OIT; `--freecad` mandatory; API-driven preselection, not physical hover |
| NaviCube | seven orientations (six axis-aligned plus one oblique); production GL vs WGPU RGB MAE <6 and label-contribution mask disagreement <35% | labelled/unlabelled pairs isolate text, including translucent labels; front text/rear occlusion evaluated against independent GL traversal |
| NaviCube modes | object + weighted_oit × opaque + translucent; Vulkan + BGFX/OpenGL | all four combinations, all seven PNG pairs retained |
| depth | overlapping near/far cubes; all eight functions, test off and write off; GL MAE <3 | unsupported implementation fails, never skipped |
| polygon-offset | coincident differently colored geometry with offset off/on; GL MAE <3 | missing support is a recorded FAIL pending explicit capability contract |
| SoAnnotation | green geometry behind red cube inside annotation; independent GL ordering oracle | actual node traversal |
| foregroundroot / decorationroot | explicit UNSUPPORTED | private FreeCAD viewer wiring not exposed by Quarter; needs full viewer instrumentation |
| axis cross / rubber-band | explicit UNSUPPORTED with expected_failure=true | legacy viewer drawing not exported as WGPU nodes; not falsely claimed as rendered |

The NaviCube oracle renders the actual FreeCAD SoNaviCube, using fixed viewport,
size, font, label textures, six axis-aligned orientations and one oblique orientation. It compares two
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

## FreeCAD delayed overlays and grid

`freecad-delayed-overlays` requires `--freecad` and
`COIN_TEST_DELAYED_HELPER` pointing to libFreeCADDelayedOverlayTestBridge.so
built by freecad-overlay-tests. It runs real SoFCPathAnnotation, SoDrawingGrid
and So3DAnnotation inside FreeCAD, on an offscreen BGFX target (Qt offscreen;
no native document viewport). Four cells cover Vulkan/OpenGL and object/weighted
OIT. PNG readbacks verify queue/depth ordering, path restoration/removal,
bounding boxes, detail invalidation and grid shrink. The Part fixture additionally
covers placed/rotated real ViewProviders, object/Face1 and local/projected ×
tight/loose bounds. Opaque depth-disabled bbox edges survive near/far clipping
in orthographic and perspective projections while lateral clipping and scene
depth remain intact. This is not a Coin/GL
workbench comparison. Missing helper is UNSUPPORTED, GPU failures are FAIL.

`freecad-grid` uses the native FreeCAD viewport and requires an unlocked
desktop. Eight cells also cover DPR 1x/2x. It checks visible screen-space
grid, camera independence, resize, removal and idle. Locked captures are SKIP.
Neither test enables the experimental adapter for production by default.

`freecad-path-selection` exercises the production Selection API in a native
FreeCAD viewport, not a test-injected annotation. OnTopWhenSelected creates
exactly one SoFCPathAnnotation for Face1, Face2 and whole-object selection.
An opaque enclosing object occludes normal rendering; visible changes must
therefore come from the on-top path. The case checks switching, resize,
removal (pixels and path count), and idle, with Vulkan/OpenGL, both transparency
modes and DPR 1x/2x. Initial fitAll prepares the complete fixture only; no zoom
or forced redraw is used to update selection. This is API-driven interaction,
not physical mouse hit-testing or the ambiguity-resolution popup.
Desktop matrices must run serially: overlapping windows invalidate captures.

The path-selection case also requires FreeCAD to remain the active window
during each capture. Obstruction/loss of focus returns SKIP with an explicit
reason, rather than treating another application's pixels as renderer output.

For the path-selection fixture, pixel comparisons use the central 25%..75%
width band containing the entire centered target (the enclosing occluder is
three times larger). Corner desktop notifications and the unrelated axis
overlay are excluded; pixel-change/removal thresholds remain unchanged.
Original full PNGs are retained for review.

## Production ambiguity-resolution menu

`freecad-selection-menu` requires `COIN_TEST_DELAYED_HELPER` and the real
native FreeCAD viewport. It uses the production SelectionMenu::doPick modal
popup with two geometry candidates; the helper drives its hover handlers
and confirms Face2 through the actual Qt submenu/keyboard event path.
The fixture hides the target inside an opaque object. Screenshots assert
visible on-top preselection for Face1/Face2, no paths/pixels after cancellation,
Face2 selection after confirmation and no residues/idle redraw after clear.
Internal menu-active and preselection state must be reset on return.
It does not test physical ray picking or the shortcut that opens the menu.

Native captures now query X11 _NET_ACTIVE_WINDOW, not only Qt's possibly
stale isActiveWindow during modal loops. An unavailable/foreign active window
is SKIP. The popup scenario is not attached to the offscreen readback case:
the attempted Qt offscreen integration timed out and is not a passing test.

After closing the modal popup, two unchanged frame-counter samples 500 ms
apart are required before the strict 1200 ms idle check. Settling is bounded
to 3 seconds and never forces redraw. test_menu_idle.py executes the actual
macro functions and rejects both a continuous loop and any frame during idle.
The native matrix with this settling phase remains pending if the X11 focus
gate reports SKIP; do not claim a complete pass from earlier partial runs.

## Isolated native GPU session

Use run_isolated.py instead of run.py to own an authenticated X server,
Metacity, a private D-Bus session and private XDG runtime/data/config/cache
directories. The desktop session and its lock/notification settings are not
changed. The runner only probes screensavers that already own a D-Bus name;
it never auto-starts one for a status query.

Hardware presentation in the tested environment requires rootful Xwayland
on Weston with the X11 backend and GL renderer. Xvfb and Xephyr lacked DRI3
and advertised llvmpipe; Vulkan adapter discovery did not produce visible
pixels there. These failed runs are not proof of GPU presentation.

Example (set the matching runtime/library environment and test helper first):

```sh
python3 testsuite/qt-quarter/run_isolated.py \
  --server xwayland --weston-prefix /path/to/extracted-weston \
  --harness /path/to/FreeCAD --freecad /path/to/FreeCAD \
  --artifacts /tmp/isolated-selection \
  --case freecad-selection-menu --case freecad-path-selection \
  --renderer vulkan --renderer opengl --require-hardware --timeout 45
```

Omit --weston-prefix for an installed Weston. For a local prefix, extract
the distribution-matched weston and libweston-13-0 packages without installing
them globally; the wrapper sets module and library paths for Weston only.
Nested Weston uses the caller's X11 display, but captures come exclusively
from its private rootful Xwayland server; covering the nested host window
does not substitute another application's pixels.

The runner forwards all test exit statuses. It stops only its own WM/server/
compositor, shuts down the private bus before removing its temporary runtime
and retains logs, GPU inventory, session metadata and PNGs in artifacts.
The temporary authentication cookie is never included in artifact metadata.

Validated matrix: /tmp/freecad-isolated-xwayland-final/results.json,
16/16 PASS (popup and on-top path selection), physical AMD GPU, no fallback
or SKIP. This is not a direct Coin/GL image comparison or physical mouse
ray-picking test, and not a claim of complete renderer parity.


## Native mouse hit-testing

Run `--case freecad-mouse-picking` with `run_isolated.py --server xwayland`
and the same hardware environment above. This case requires the wrapper's
private X11 marker and refuses to move the desktop mouse from a plain run.
XTest injects native pointer motion and left-button press/release through the
actual viewport; no Qt sendEvent, addSelection or setPreselection creates the
positive result. A read-only getObjectInfo/SoRayPickAction scan chooses two
visible face interiors and supplies expected face identifiers. Coin pick
coordinates are physical pixels with bottom-left origin; XTest uses physical
root coordinates, and Qt widget coordinates are logical pixels.

Checks: Face A hover, Face B hover, click selection of B, background clearing,
visible pixel changes, cleanup, resize/re-pick and hover clearing, then bounded
settling (at most 3 seconds) and strict 1.2-second frame-free idle. Renderer,
physical GPU submission and absence of GL fallback remain mandatory.
Negative unit controls reject wrong objects/faces and continuous redraw.

Coverage is a real Part box in an axonometric viewport, not universal picking
parity: edges/vertices, transformed assemblies, perspective and selection
modifiers remain separate follow-ups. This is automated native mouse input,
not a claim that a human physically moved the mouse.


Final native mouse matrix: /tmp/freecad-mouse-picking-final/results.json,
8/8 PASS, no SKIP or fallback, physical AMD GPU, wrapper exit 0.
Python regression suite: 29/29 PASS.


## Topology and camera mouse paths

`--case freecad-mouse-elements` uses the same isolated hardware session;
allow `--timeout 75`. It covers native hover and click on a face, edge and
vertex in orthographic, perspective and translated/rotated Part::Feature
scenes, with per-element visual evidence and cleanup. Vertex pixels are
counted densely near the pointer, not sparsely over the whole viewport.
The B-rep line/point callback path now consumes production selection
contexts and shares retained overlay geometry/depth semantics with GL.
Assemblies/App::Link and selection modifiers are not implied by Placement.


Topology matrix: /tmp/freecad-elements-final/results.json, 8/8 PASS,
physical GPU and no fallback, wrapper exit 0. Uses 600ms event settling
and records native pointer plus Coin motion positions. The earlier 350ms
matrix had a single preselection mismatch; retain that failure evidence.
Nested Weston now uses --no-input so desktop pointer events are not forwarded;
XTest still injects directly into the private X11 server.


## App::Link and selection modifiers

The new `freecad-mouse-links` case creates two App::Link instances of a
hidden Part box, separated by LinkPlacement. Native XTest input exercises
instance-specific hover/click, Ctrl additive selection and toggle, Shift
replacement, cleanup and resize/re-pick. Per-instance image regions reject
highlight leaking from one instance into the other. Coin mouse events must
carry the injected Ctrl/Shift flag; no selection API creates the result.

Use the usual Xwayland/hardware runner with `--case freecad-mouse-links`
for the BGFX 8-cell matrix. That matrix has NOT been executed here: the
previous /tmp BGFX/FreeCAD/Weston installs were no longer available.

A separate Coin/GL baseline is supported explicitly:

```sh
python3 testsuite/qt-quarter/run_isolated.py --server xvfb \
  --harness /path/to/FreeCAD --freecad /path/to/FreeCAD \
  --case freecad-mouse-links --renderer opengl --reference-gl \
  --artifacts /tmp/link-gl-reference --timeout 60
```

This emits REFERENCE_PASS, never BGFX PASS; --require-hardware is forbidden
in this mode. Only object mode and DPR 1/2 are relevant to the GL baseline.
Reference labels are rejected by the default BGFX gate even if the log also
contains a BGFX submission line. The GL baseline does not assert a BGFX frame
counter or idle loop and is not a same-build image-parity comparison.

The older persistent FreeCAD/Coin-GL build passed the expanded per-instance
scenario at DPR 1, and DPR 2 passed on a fresh-session retry. The original DPR
2 process crashed during initialization; its FAIL is preserved in
/tmp/freecad-links-gl-instance-isolation. The retry lives in
/tmp/freecad-links-gl-instance-dpr2-retry. Python suite: 43/43 PASS.


## Persistent reboot recovery

The previous missing-/tmp notes are historical. The recovered runtimes,
build scripts and evidence now live under Coin's ignored
`build-bgfx-recovery/`; see [recovery notes](../../docs/coin-bgfx-recovery.md).
The App::Link hardware matrix passed 8/8 and the same-build Coin/GL
reference passed 2/2 before the document-tab clipping correction.

The private Xwayland runner now uses headless Weston with its GL renderer,
checks the actual 2400×1600 X screen, and has no host input surface. On
this hybrid GPU machine the scripts pin Mesa/AMD only in the test process
environment. They do not change desktop locking or GPU preferences.
Only the new private App::Link profile disables dock overlays; existing
profiles are preserved.

The link test rejects pointer targets covered by dock buttons and holds
Ctrl/Shift until Coin acknowledges the native click. After resize, it
also compares overlapping document-tab pixels with independent Qt painting.
DPR 2 must actually exercise tab overlap; an absent sample cannot pass.
The native Vulkan weighted_oit DPR 2 clipping probe passed, including idle.
Python controls: 60/60 PASS. Native results, GL references, and image
measurements are separate evidence; no one is a blanket visual-parity gate.


Final post-clipping evidence: `artifacts/links-bgfx-clipped/results.json`
has 8/8 hardware PASS; `links-gl-clipped/results.json` has 2/2
REFERENCE_PASS from the same build. All four DPR 2 cells sampled 4494
document-tab pixels with RGB MAE 0. The 88 same-build image measurements
are in `links-visual-clipped-measurements.json`, not a blanket parity claim.


## Nested links and assembly topology

`freecad-mouse-link-topology` covers nested shape links, links to nested
App::Part containers, and nested links to those assemblies. Placements are
composed with LinkTransform in the nested cases. Two instances each exercise
Face/Edge/Vertex hover and click, Ctrl additive selection, per-instance
pixel isolation, cleanup, and a complete re-pick after resize (24 topology
interactions per cell). The native BGFX gate additionally checks idle.
Source resolution is explicitly disabled when reading selection identities:
`getSelectionEx(document, 0)` must retain the root instance and full path.
Fixtures use 6px points and 2px lines, not a relaxed pixel threshold.

Use `--case freecad-mouse-link-topology --timeout 300`. Native runs require
`--require-hardware`; GL reference runs use `--renderer opengl --reference-gl`
and are never BGFX PASS. Assembly workbench joints/solver, external-document
links and Link arrays are outside this minimal Part-based fixture.

The planar lighting gate now has a tracked command:

```sh
python3 testsuite/qt-quarter/compare_link_lighting.py \
  --native /path/to/native-link-matrix \
  --reference /path/to/same-runtime-gl-reference \
  --output /path/to/lighting-parity.json
```

It rejects missing cells/captures, mismatched geometry, empty foreground,
and baseline foreground RGB MAE above 1. It preserves the union mask and
RGB threshold 8. Use the same FreeCAD/Coin libraries for both matrices.
This is not a Gouraud-versus-fragment-PHONG parity test for arbitrary meshes.

The runner now atomically checkpoints completed results after each cell.
An interrupted matrix is partial, not approval of unexecuted cells. The
lighting gate still requires all eight native cells and both GL references.
