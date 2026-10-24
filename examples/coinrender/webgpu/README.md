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
