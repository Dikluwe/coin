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
