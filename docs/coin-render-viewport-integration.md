# Integrated viewport validation

The development checkout combines the focused changes from:

- Coin `7301a52d60`: polygon offset and depth range.
- Coin `0bfd6b5f5d`: external-renderer frame preparation.
- FreeCAD `4fe05cccd7`: native Qt viewport without auxiliary GL preparation.
- Coin `b0c2ac3ce2`: Qt/Quarter regression harness.

Only the focused commit diffs were applied; isolated-worktree baseline snapshots
were not imported. Existing unrelated changes remain in the development trees.

The harness reads the production `wgpuFrameCount` property to verify fresh
frames. Counting `actualRedraw()` would incorrectly report failure because the
native viewport deliberately no longer invokes that OpenGL entry point.

Builds: `/tmp/coin-bgfx-evaluation-build`, `/tmp/freecad-wgpu-build2`.
Coin installation: `/tmp/coin-priority1-install`.

## Integration fixes

- Preserve traversal order when opaque depth comparisons, depth test/write,
  depth ranges or polygon offset make material grouping order-sensitive.
- Request a coalesced native frame on devicePixelRatioChanged.
- Destroy the native presenter before constructing the fallback GL viewport;
  create that widget without a parent until setViewport reparents it safely.
- Read the GL oracle with raw glReadPixels RGBA, avoiding QImage alpha
  unpremultiplication that manufactured translucent-only RGB differences.
- Count native production frames rather than auxiliary GL redraws in tests.
- Preserve total traversal order within overlay layers in composition and
  opaque grouping, and submit each overlay in one immediate sequential view.
- Retain a callback-only SoAnnotation wrapper around the FreeCAD NaviCube,
  publishing its overlay layer and scissored depth clear without changing GL.

## Verified integrated results

Coin and FreeCADGui/FreeCADMain compile successfully against the updated Coin
installation. The relevant BGFX matrix passes 14/14, without skips, including
depth range/offset, object blending, sorted layers, weighted OIT and annotations
in Vulkan and OpenGL. cargo check passes with existing warnings.

The real FreeCAD PartDesignExample smoke passes on the X11 desktop with both
Vulkan and BGFX/OpenGL. It covers first expose, wheel, camera animation,
selection/preselection, resize, maximize/restore, bottom-panel open/close,
minimize/restore and Qt mouse events switching between interior face targets.
After animations settle, the idle stage records zero additional frames.
The gdb probes record zero auxiliary Qt/Coin GL traversals, and logs contain
no QOpenGLContext/QRhiGles2 warnings. Logs are saved as:

- `/tmp/freecad-integrated-vulkan-smoke.log`
- `/tmp/freecad-integrated-opengl-smoke.log`

The real NaviCube GL/BGFX comparison passes all eight configurations:
Vulkan/OpenGL x object/weighted OIT x opaque/translucent. Each configuration
covers seven orientations, including an oblique view, with both RGB and
front/rear-label mask assertions. The maximum translucent RGB mean error is
0.464 on OpenGL and 0.583 on AMD Vulkan (original limit: 6). No comparison
threshold was relaxed. Depth/polygon-offset/annotation comparisons also pass
3/3 in both renderers. The runner gate passes 8/8.

Artifacts and the detailed matrix are described in
`testsuite/qt-quarter/EXECUTION.md`.

Qt harness DPR tests pass at 1x and 2x. The original integration explicitly
rejected a second simultaneous BGFX target without crashing the first.
The subsequent shared-runtime implementation replaces that limitation:
two-viewports now requires real simultaneous rendering, independent camera
updates and survival after destroying the initialization owner. See
`bgfx-multiple-viewports.md` for current limits and validation.
Real monitor-to-monitor DPI migration, Wayland/Windows/macOS and private
GL-only FreeCAD overlays remain outside this validation.
Headless maximize without a window manager is an explicit skip.
