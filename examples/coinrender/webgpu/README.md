# CoinRender WebGPU demonstration

This isolated profile captures a perspective camera, opaque cube geometry and material
with the real C++ Coin/CoinRender traversal in WebAssembly. A targetless
`CoinRenderAction` produces its frame plan; the RECORDING build is used only for
capture, with no reference/software rasterization. An explicit JavaScript bridge
copies captured clip-space positions, normals, material colors and indices into a
second WebAssembly module. Rust **wgpu 24** selects **BROWSER_WEBGPU only** and renders
the captured triangles on the canvas. The renderer contains no authored cube mesh.
Movement changes the Coin rotation node and recaptures the frame; resizing updates
the Coin viewport and the wgpu surface. This is a small demonstration profile,
not a claim of complete parity with the native backend or full Coin scene support.

The complete presentation, including canvas, diagnostics and controls, fits a
centered 16:9 stage that uses the largest size contained in the viewport. Other
screen proportions show letterboxing or pillarboxing. Long status/adapter text
and profile details scroll inside their own panels, leaving the stage and pause
control in place. Keyboard users can focus these panels to scroll their contents.
The canvas keeps its actual viewport proportions for Coin's camera capture.

Select the WebGPU feature profile explicitly through the page URL:
`?profile=core` (also the default when absent) or `?profile=compatibility`.
For example, `https://192.168.0.16:8443/?profile=compatibility` requests only that
profile. Empty, invalid or repeated `profile` parameters fail before GPU/WASM
initialization. `window.coinRenderWebGPU.profile` and the adapter panel report
the requested profile. The same adapter request made by Rust wgpu receives that
exact `featureLevel` and `forceFallbackAdapter: false`; the page never retries
core as compatibility or switches profile automatically.

WebGPU compatibility is a WebGPU API profile that may be implemented over
hardware GLES. It is not evidence of a Vulkan driver and does not mean the page
uses a WebGL fallback. Rendering still uses the Coin WASM capture and Rust wgpu
WebGPU pipeline, with the same real adapter checks and GPU completion requirement.
Requesting compatibility alone does not prove that all required device/pipeline
operations will work on a particular browser or TV; actual errors remain visible.

Build with Python 3.9+, CMake, Ninja, Emscripten, Rust's
`wasm32-unknown-unknown` target and `wasm-bindgen-cli 0.2.125`. The Emscripten
SDK must be activated in the environment. Toolchain provisioning may need network
access. All disposable output, builds and caches must be under a task directory
in `/dev/shm`; the builder checks this and accepts provisioned RAM caches.

```sh
python3 .github/scripts/build-coin-render-webgpu.py \
  --build-dir /dev/shm/coin-webgpu-demo/build \
  --output-dir /dev/shm/coin-webgpu-demo/site
```

Use `--emcmake`, `--cargo` and `--wasm-bindgen` for explicit tool paths.
The build reports artifact creation, not successful browser rendering.

Serve only the artifact directory using a trusted TLS certificate. For a LAN
with private IP addresses, use a local development CA and install its public
certificate in each client device. HTTPS encryption alone or bypassing a browser
warning does not establish trusted HTTPS. A public CA cannot issue for a private
RFC1918 address; a public domain with DNS validation is a separate option.

For Chrome/Chromium on Linux, mkcert can install a development CA in the user's
NSS trust store without changing the system-wide store. Restart the browser after
installing trust. Keep CA and server private keys in a permission-protected user
folder on a filesystem that enforces permissions, outside Git and outside the
published artifact directory. Do not keep private keys on a shared NTFS volume.
For example, with mkcert and certutil installed:

```sh
umask 077
export CAROOT="$HOME/.local/share/coinrender-webgpu/tls/authority"
mkdir -p "$CAROOT"
TRUST_STORES=nss mkcert -install
mkcert \
  -cert-file "$HOME/.local/share/coinrender-webgpu/tls/server.pem" \
  -key-file "$HOME/.local/share/coinrender-webgpu/tls/server.key" \
  localhost 127.0.0.1 ::1 192.168.0.16
python3 examples/coinrender/webgpu/serve.py \
  --directory /path/to/built/site \
  --cert "$HOME/.local/share/coinrender-webgpu/tls/server.pem" \
  --key "$HOME/.local/share/coinrender-webgpu/tls/server.key" \
  --bind 0.0.0.0 --port 8443
```

Replace `192.168.0.16` with the server's LAN address before issuing the certificate.
Open `https://192.168.0.16:8443/` remotely or `https://localhost:8443/` on the PC.
The server exposes only six artifact filenames, refuses symlinks and never lists
directories. Keep keys, certificates and server logs out of Git and outside the
site directory. Transfer only the **public** `rootCA.pem` to a second device,
optionally named `CoinRender-Local-CA.crt`; never transfer `rootCA-key.pem`.
Android/Google TV certificate installation depends on its firmware and browser.
The client must explicitly trust the CA for websites; TV trust is not implied by
installation on the PC. If using ADB to transfer the public certificate, finish
installation through the TV's credential settings and disable debugging afterward.

A TV browser must itself implement WebGPU, expose `navigator.gpu` in a secure
context, and provide a usable adapter/device. HTTPS does not add WebGPU to an
incompatible browser. There is no WebGL/software/image fallback. The page reports
errors for unavailable contexts/adapters/devices and validates each submission
before counting a frame. During Rust initialization a temporary JavaScript hook
checks the **same GPUAdapter returned to wgpu**, without requesting an extra
adapter. It publishes vendor/device/architecture/description and the real
`isFallbackAdapter` property (old and current WebGPU locations), rejects fallback
adapters and known software identities such as SwiftShader/llvmpipe. A real
explicit `isFallbackAdapter: false` is accepted even when the browser hides all
identity fields; the page reports that the identity is not provided/hidden by
the browser, and the observable adapter fields remain empty. This does not infer
a hardware manufacturer or model. Any contradictory `true` flag is rejected,
and absence of an explicit non-fallback flag remains an error.
The same hook retains the exact device returned to wgpu. Rust encodes, submits,
presents and validates each frame; JavaScript then awaits that device's native
`queue.onSubmittedWorkDone()` before publishing success, since wgpu 24's web
queue completion wrapper is not implemented. Device loss and uncaptured errors
stop publication. Original `requestAdapter`/`requestDevice` methods are restored
after initialization. `window.coinRenderWebGPU` exposes status, actual backend,
`fallback: false`, `sceneSource: 'coinrender-wasm'`, real captured vertex/index
counts, confirmed frame count and error text. A successful build alone does not
prove that a particular TV supports WebGPU.

## Browser configuration and qualified scope

On the Linux host, Firefox 158.0.1 did not expose `navigator.gpu` in its default
configuration. A separate temporary profile with `dom.webgpu.enabled=true`
rendered the captured cube over the LAN HTTPS address, with certificate checks
enabled, confirmed queue completion and `isFallbackAdapter=false`. Firefox
redacted all adapter identity strings; the page reports that absence without
inventing a manufacturer. This does not qualify the default Firefox profile.
To opt in manually, open `about:config`, set `dom.webgpu.enabled` to `true`,
and reload the demonstration. Enabling an experimental API is a browser choice,
not something a website can perform. No ordinary browser profile is modified
by the example.

The embedded Codex browser opened HTTPS but returned no WebGPU adapter in this
campaign. A WebGL checker can still render there through WebGL 2. A result for
WebGL 2 is not a WebGPU result; this demonstration never switches to WebGL.

Initialization reports its current stage in the status panel and
`window.coinRenderWebGPU.phase`: ES modules, Coin WASM, renderer WASM, adapter,
device, pipeline/canvas configuration, first frame, submission and GPU completion.
Each asynchronous stage has a 30-second watchdog, including static ES imports
before the application module runs. A timeout produces a terminal error naming
the stage. Late promises cannot resume rendering or turn that error into success;
reload the page to retry. These diagnostics do not enable fallback or bypass TLS.

## Local Firefox Linux troubleshooting

The local Firefox 158.0.1 / Mesa 25.2.8 investigation observed a main-thread
`SIGSEGV` at address `0x18` in `libgallium` while running this demonstration.
The CoinRender crash reproduced in a fresh temporary Firefox profile. A minimal
WebGPU clear-only control survived a 35-second observation; a triangle with a
depth attachment did not reach queue completion in the default graphics
environment, but completed with the AMD ICD selected. These controls distinguish
local rendering paths; they do not establish a Coin or browser fix.

A further native WebGPU control, with no Coin, Rust or WASM, rendered a triangle
with a depth attachment on a **1321 × 853 canvas** and also crashed Firefox
158.0.1 at `SIGSEGV 0x18`. Its seven `libgallium` frame offsets and first
`libxul` frame offset (`0x4cb243f`) matched the demonstration's observed crashes.
Reproduction outside Coin strengthens the evidence for a shared browser/graphics
path problem in this local environment; it does not identify the underlying bug
or establish behavior on other versions or machines. The standalone source is
[`firefox_native_repro.html`](firefox_native_repro.html).

In a separate temporary profile, setting `widget.dmabuf.enabled` to `false` in
`about:config` and **restarting Firefox** allowed the same demonstration to confirm
337 frames with `isFallbackAdapter=false`. Firefox logged:

```text
WebGPU: disabling SharedTexture swapchain: missing GBM_FORMAT_ARGB8888 dmabuf format
```

This is a local diagnostic workaround for that observed environment, not a
recommended global setting or a correction to Coin, Firefox or Mesa. If
investigating the same symptom, use a separate temporary profile, record the
original preference, and restart Firefox after changing or restoring it. The
demonstration does not change the normal Firefox profile automatically. The
observation does not guarantee longer-term stability or results on other hosts.
The tested Linux profile also required `dom.webgpu.enabled=true` and a trusted
HTTPS certificate; disabling dmabuf does not replace either requirement and does
not bypass certificate validation.

### Run the native control manually

`firefox_native_repro.html` is a standalone diagnostic control excluded from the
application site build and production server allowlist. It uses native WebGPU
and exposes `window.probe` (`phase`, `adapter`, confirmed `frames`, and caught
`error`). A native crash or stalled completion can prevent further updates; the
control preserves that behavior and does not qualify an adapter as hardware.

Copy only this HTML into a separate static-server directory, for example:

```sh
mkdir -p /dev/shm/coin-firefox-native-repro/site
cp examples/coinrender/webgpu/firefox_native_repro.html \
  /dev/shm/coin-firefox-native-repro/site/index.html
```

Serve that isolated directory with a separate HTTPS server and the trusted
localhost certificate described above, keeping keys outside the served tree.
Open its HTTPS address in a temporary Firefox profile and inspect `window.probe`
in the console. The tested Linux profile required `dom.webgpu.enabled=true`;
keep certificate validation enabled. Compare graphics preferences deliberately
in that temporary profile and stop the server afterward. The production site
build and server access rules do not need to change for this reproduction.

The demonstration requests render-only resource limits, since its vertex/index
and single color/depth pipeline uses no compute or storage resources. Rust uses
`Limits::downlevel_webgl2_defaults().using_resolution(adapter.limits())` to avoid
requiring unused compute capabilities while retaining the adapter's texture
resolution limits. The preset name describes numbers only: the actual backend
remains `BROWSER_WEBGPU`, with no WebGL renderer or fallback.

## Android TV HTTPS viewer

The [scoped HTTPS viewer](android_viewer/README.md) loads this same WASM site
using the TV's installed WebView. It trusts the supplied public CA only for the
configured host, without changing system/browser trust or bypassing TLS. The
tested TCL Android 11 / Mali-G52 / WebView 155 configuration rendered the cube
with `?profile=compatibility`, ARM `isFallbackAdapter=false`, and advancing GPU
completion counts. Core was unavailable there. A different CA was rejected.
The renderer requests render-only limits rather than unused compute capabilities;
the original 256-invocation compute requirement exceeded this adapter's 128 limit.

## Direct TCL browser verification

The same six site artifacts also rendered directly in the TCL BrowseHere browser
on the tested Android 11 TV, using trusted public HTTPS and the explicit
`?profile=compatibility` URL. The captured cube showed 24 vertices, 36 indices
and advancing confirmed GPU frames with ARM `isFallbackAdapter=false`. The
foreground activity was `com.tcl.browser`; rendering continued after removing
the diagnostic viewer. The viewer and its app-specific CA trust are therefore
not required for this browser path. No WebGL or software fallback was used.

A temporary public HTTPS tunnel can provide the browser-trusted certificate when
the TV firmware cannot install a local CA. This exposes the demonstration
publicly and is a separate hosting choice from LAN-only access. Keep the existing
artifact-only server boundary; the tested tunnel verified the local HTTPS origin
against its CA as well. Open the issued public address with
`?profile=compatibility`; its lifetime depends on the tunnel and local server.

## Instanced city benchmark

Use `?profile=compatibility&scene=million` for exactly 1,000,000 buildings plus
one ground cube (12,000,012 submitted logical triangles), or `scene=city-40000`
for the explicit 40,000-building control. The default remains the original cube.
Unknown or repeated scene selectors fail before initialization. Nothing switches
scene/profile or reduces the instance count automatically.

The build generates `city-million.bin` and `city-40000.bin` directly from the same
seed-136, eight-color authored decimal records used by `generate_large_scene.py`;
its IV output semantics stay the same. The browser receives compact transforms
and palette indices rather than a million-node IV file. C++ creates at most 1024
Coin instances per batch and captures their transforms/materials through a real
targetless `CoinRenderAction`. Each captured cube's positions, normals and indices
must match the captured canonical `SoCube` before that batch is uploaded. The
scene graph and expanded frame plan are released per batch. Rust wgpu retains
one captured mesh and all 1,000,001 captured instance records in an 80,000,080-byte
GPU vertex buffer and draws every instance with indexed instancing. Oversized
allocations fail against the actual adapter/device `maxBufferSize`; no sampling,
LOD or proxy-only drawing is used. Nonuniform-scaled normals use cofactors for
the inverse transpose. Geometry, instance and depth buffers persist while the
scene/extent is unchanged; only captured camera/light uniforms are sent per frame.

The fixed camera uses Coin's perspective camera, pi/4 height angle, orientation
from `(0,0,-1)` to `(-.5,-.35,-1)`, and `viewAll` factor 1 over the exact authored
city bounds and current viewport. The bounding cube is used only to fit that
camera, never as the rendered city. The default city stays static during steady
measurement; resizing refits the camera and starts a separate warmup/measurement.

The page reports capture progress and `state.scene` counts, canonical mesh counts,
actual canvas extent and GPU-completed frames. `submittedInstances` stays zero
until the full draw has completed on the actual native WebGPU queue. After exactly
10 completed warmup frames, `state.benchmark.groups` contains groups of 10 frames:
`frameMs` is elapsed wall time / 10 (including scheduling/RAF), `fpsWall` is
`1000/frameMs`, and `renderMs`/`completionMs` are mean wall intervals spent in
wgpu encode/submit/validation and the native completion await. These are not GPU
timestamps. Histories are bounded to 120 groups, with FPS p50/p95 over those
groups. Startup asset load, Coin capture and GPU upload are reported separately;
startup `totalMs` is navigation-to-ready wall time. No memory-use estimate or
extrapolated throughput is reported.

For the TV's ordinary BrowseHere browser without changing its certificate store,
serve only on PC loopback and use the authorized ADB reverse connection:

```sh
python3 examples/coinrender/webgpu/serve.py --directory /dev/shm/coin-city/site \
  --loopback-http --bind 127.0.0.1 --port 8089 --benchmark-reports
adb -s 192.168.0.18:5555 reverse tcp:8090 tcp:8089
```

Open `http://127.0.0.1:8090/?profile=compatibility&scene=million&report=1` in
BrowseHere (its address bar may search for the hostname `localhost`). Loopback
provides the browser secure context; this mode does not
ignore TLS failures or create a public HTTP listener. HTTPS remains the server's
default and requires its certificate/key outside the served directory.
`--loopback-http` rejects non-127.0.0.1 binding. No public tunnel is required.

`report=1` explicitly POSTs a bounded own-page measurement snapshot to
`/benchmark-report` after completed groups. This endpoint exists only with
`--benchmark-reports` on the loopback HTTP server, validates matching Origin/Host
and a JSON object of at most 64 KiB, and prints `BENCHMARK_REPORT` JSON to stdout.
It accepts no client filesystem paths and exposes no report/log files. Export
failure appears in `state.reportError` and never changes real GPU success. Omit
`report=1` to disable exports; the normal HTTPS server has no telemetry endpoint.

Historical native Android emulator measurements (~45.80–48.73 FPS, ~20.676 ms,
1080×2400) traversed the native scene/rendering path. This browser benchmark uses
captured instancing on different TV hardware/runtime and reports its actual canvas
resolution. Those methods and extents must be stated together; they do not support
a hardware speedup ratio. The G7 million-scene startup/memory failure has no valid
FPS and must not be treated as zero FPS or extrapolated from the 40k control.

The TCL million trial is **not qualified**: preparation took about six minutes and
completed full submissions produced a uniform clear image. The explicitly selected
40k control did render visible geometry. Use that control for the TV demonstration;
the TCL 40k trial recorded **8.50 FPS median** after **17.02 s of preparation**,
while the million trial took **365.68 s to prepare** and showed a clear image. These are different qualification
results, not a performance ratio. The original measured-scope report is preserved
in [the local companion archive](../../../docs/coin-render-evidence.md) at
`integrated/docs/coin-render-webgpu-city-tv-20261023.md` or its historical prefix.
The temporary public tunnel used for the earlier cube trial has been stopped.
