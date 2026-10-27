# Public sampling API qualification

`run.py` uses the public tests' explicit native/portable options, runs GPU work
sequentially and records the command, environment, hashes, exit status and log.
Any FAIL/SKIP produces a nonzero result; the ledger preserves both. It requires
Linux with an accessible display, wgpu/BGFX builds and libraries in
`build-{backend}/{bin,lib}` under the artifact root. XAUTHORITY is inherited.
Default profiles are AMD Vulkan/OpenGL and NVIDIA Vulkan for both backends;
use `--profiles` to restrict hardware.

The results discussed here belong to the recorded historical revisions. Archived
reports and application fixtures retain their original paths in the companion
archives; see the [evidence guide](../../../docs/coin-render-evidence.md).

```sh
root=/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name stock-portable-repeat --modes portable \
  --cases api,deep,direct,viewport,procedural
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name native-resources-repeat --cases selection,publication,ownership,large,devices,stress,instancing,shadow-eight,shadow-viewport,shadow-alpha
```

`api` exercises both policies in one process; selection/publication/ownership,
resources/instancing/shadows are independent native controls. Backend-exclusive
wgpu/BGFX cases are filtered. `COIN_SAMPLING_STUDY=fetch` is injected as an
intentional conflict: the public API must ignore the old laboratory selector.

When a comparison requires the laboratory-only patched AMD CoinGL reference:

```sh
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name mesa-public-repeat --modes native,portable \
  --cases projective,sampling,advanced,rtt \
  --mesa-prefix /mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-failure-fixes/mesa-study/install
```

The Mesa prefix is optional and external to the renderer. It changes only AMD
child-process environments and disables caching for the study. Compare installed-
driver and study-driver cohorts separately. A corrected CoinGL reference does not
fix the installed driver. Preserve the actual environment, builds and ICDs.

Older `portable-sampling-study` runners reject the public API to prevent a
fetch/fine-labeled measurement from silently running native sampling. Use the
frozen baseline `43f00b0e18830ba79fe8fd19020d1f8ececa4983` for historical
reproduction and this runner for the public policy. Builds are mutable; each
cohort's logs identify the binaries actually used.

Contract, limits and ledger: [API](../../../docs/coin-render-texture-sampling-api.md).

## Linux continuation: window, performance and historical FreeCAD controls

The window and benchmark runners remain in this checkout. The FreeCAD commands
in the following historical recipe require the separately preserved application
integration; `qualify_freecad_linux.py`, its host patches and macros are excluded
from this contribution. Do not treat them as runnable from this checkout alone.

```sh
root=/mnt/Laranja/Git/externos/coin-portable-sampling-artifacts/20261008-sampling-api-linux
python3 testsuite/reproducers/sampling-api/run.py --artifacts "$root" \
  --name window-repeat --cases window,api,selection,publication,ownership
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name cpu-repeat --frames 90 --warmup 30
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name gpu-repeat --kind gpu --workloads texture-1,texture-4,texture-8,npot-8 \
  --frames 45 --warmup 15
python3 testsuite/reproducers/sampling-api/benchmark_linux.py --artifacts "$root" \
  --name baseline-repeat --baseline --frames 90 --warmup 30
python3 testsuite/reproducers/sampling-api/qualify_freecad_linux.py --artifacts "$root"
python3 testsuite/reproducers/sampling-api/qualify_freecad_linux.py --artifacts "$root" \
  --name freecad-dpr2-repeat --scale 2 --policies portable
```

`window` requires images from two simultaneous targets and an oracle after
resize/suspend/remap, policy independence and recovery; it also exercises the
manager and adapter. wgpu/OpenGL has no COPY_SRC on this surface, so the test
inspects physically presented X11 pixels. Other profiles use renderer RGBA
capture. The fixture confirms extent and MapNotify before submission; window
manager races are not valid resize/exposure results.

The benchmark uses ABBA ordering and does not read pixels. CPU results represent
render/present calls with possible backpressure, not display latency or completed
GPU work. The GPU probe is a separate synchronous campaign with backend-specific
scopes; unavailable timestamps remain unavailable and do not qualify the probe.
The runner requires identification of the requested physical adapter. BGFX/OpenGL
uses the GL context name with tracing only during setup because capabilities can
report vendor/device zero. The identification records and their audit appear in
the ledger. wgpu/OpenGL uses `--allow-vsync` because the surface does not offer
Immediate/Mailbox; do not compare its rate as though vsync were disabled. The
BGFX million-instance case uses 12 samples/4 warmups to bound its high cost, with
the same counts for policies and baseline. Individual commands retain the actual
configuration. `--resume` retains valid checkpoints; do not mix runtime revisions
within a final qualification cohort. The historical runner was never used to
measure this API's portable policy.

The historical FreeCAD qualification required a private rebuilt host in
`freecad-host/build`, using the archived per-viewport Qt patch at
`examples/coinrender/freecad_sampling_policy.patch`. It uses the Boolean Qt
property `coinRenderPortableSampling` and publishes
`coinRenderActiveSamplingPolicy`; changing the property recreates the adapter.
`freecad_sampling_policy.FCMacro` reapplies existing screen-content gates without
reducing tolerances. The projective case uses a POT 256×256 image and quality 0.5
to exercise NEAREST_MIPMAP_LINEAR/minification in the host, alongside its existing
content. Two redraws establish the camera before the baseline; a visible/hidden
cycle prepares text resources before the main baseline. The qualified profile
is warmed and declares `cold_start_qualified=false`; it does not certify the
absolute first frame or fix the initial border transition recorded in the
NVIDIA/wgpu portable study.

The macro blocks interactive input only in the private host, closes its own
menus/tooltips, requests ABOVE through EWMH (`wmctrl`/`xprop`) without recreating
the Qt surface, and requires two identical render/screen-read results within
0.5 seconds. This bounds presentation without weakening image gates. Cleanup
must stabilize within 5 seconds before the original 1.2-second idle gate. It
requires an X11 window manager with ABOVE support. The runner freezes three
macros in `fixture/` before starting the host and records a SHA-256 manifest per
run. It requires exactly one result per process, a physical backend submission
and the requested active policy. It uses object/DPR 1 or 2 with forced
QT_SCALE_FACTOR; this does not certify monitor changes or distinct physical DPI.
AMD/Vulkan uses a private Xwayland session; OpenGL and NVIDIA use the accelerated
desktop because the original machine's headless session does not prove DRI3 GL,
and the NVIDIA window manager exited before the host. Preferences are private
in both cases. A generic FreeCAD without this patch is not evidence for the
portable policy.

The reused host build, compile/link commands, modified source, patch, hashes and
logs are retained in the artifact root and original ledger. The original FreeCAD
host and source are not modified. The supplementary build recipe preserves the
previous host's CMake/object snapshot and rebuilds Quarter; another tree requires
a full host reconfiguration against CoinRender before using the macro. Copying
only the executable without Part/Material/libraries/resources/Pivy is insufficient.

Original contract/results report:
`docs/coin-render-sampling-api-linux-20261008.md`, preserved in the companion
archive described by the [evidence guide](../../../docs/coin-render-evidence.md).
