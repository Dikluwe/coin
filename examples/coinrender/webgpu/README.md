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

Serve only the artifact directory using a TLS certificate and key stored outside
it. For development, generate a short-lived certificate in RAM with a subject
alternative name matching the actual LAN IP or DNS name:

```sh
mkdir -p /dev/shm/coin-webgpu-demo/tls
openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
  -keyout /dev/shm/coin-webgpu-demo/tls/key.pem \
  -out /dev/shm/coin-webgpu-demo/tls/cert.pem \
  -subj /CN=coinrender.local \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:192.168.1.20'
python3 examples/coinrender/webgpu/serve.py \
  --directory /dev/shm/coin-webgpu-demo/site \
  --cert /dev/shm/coin-webgpu-demo/tls/cert.pem \
  --key /dev/shm/coin-webgpu-demo/tls/key.pem \
  --bind 0.0.0.0 --port 8443
```

Replace `192.168.1.20` with the server's reachable LAN address. Open
`https://192.168.1.20:8443/` on the other device. Configure certificate trust on
that device or use a certificate from a CA it trusts; bypassing a warning alone
may not establish the secure context WebGPU requires. The server exposes only
six artifact filenames, refuses symlinks and never lists directories. Keep TLS
keys, certificates and server logs out of Git and outside the site directory.

A TV browser must itself implement WebGPU, expose `navigator.gpu` in a secure
context, and provide a usable adapter/device. HTTPS does not add WebGPU to an
incompatible browser. There is no WebGL/software/image fallback. The page reports
errors for unavailable contexts/adapters/devices and validates each submission
before counting a frame. During Rust initialization a temporary JavaScript hook
checks the **same GPUAdapter returned to wgpu**, without requesting an extra
adapter. It publishes vendor/device/architecture/description and the real
`isFallbackAdapter` property (old and current WebGPU locations), rejects fallback
adapters and known software identities such as SwiftShader/llvmpipe, and reports
an error if the browser withholds enough information to prevent verification.
The same hook retains the exact device returned to wgpu. Rust encodes, submits,
presents and validates each frame; JavaScript then awaits that device's native
`queue.onSubmittedWorkDone()` before publishing success, since wgpu 24's web
queue completion wrapper is not implemented. Device loss and uncaptured errors
stop publication. Original `requestAdapter`/`requestDevice` methods are restored
after initialization. `window.coinRenderWebGPU` exposes status, actual backend,
`fallback: false`, `sceneSource: 'coinrender-wasm'`, real captured vertex/index
counts, confirmed frame count and error text. A successful build alone does not
prove that a particular TV supports WebGPU.
