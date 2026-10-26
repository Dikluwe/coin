# Explicit RGBA8 window capture (F15)

Request capture before submission to publish the presented frame as top-down RGBA8:

```cpp
if (target->requestWindowReadbackRGBA()) {
  action.apply(scene);
  if (action.getLastStatus() == CoinRenderAction::SUCCESS)
    target->readbackRGBA(pixels);
}
```

For scene managers, request through `manager.getRenderTarget()` then render.
The request applies to one frame. A later normal render retains no published
window pixels or staging. `borrowRGBA()` remains offscreen-only; window capture
copies through `readbackRGBA()`. Resize/suspension invalidate capture. Offscreen,
suspended or invalid targets reject the request with FALSE/diagnostic. The common
nominal RGBA limit is **128 MiB**. Failed submission preserves the last captured
frame but consumes the request; the next normal render does not retry capture.
Publish only exactly `width*height*4` backend bytes.

Common code owns per-frame policy, size/format and atomic publication. BGFX requests
swapchain screenshot only for that frame; render-thread callback normalizes BGRA/
RGBA and origin. Unique request IDs prevent delayed callbacks publishing another
frame. wgpu uses surface COPY_SRC only when offered, copies the acquired texture
to mappable staging, normalizes BGRA if needed and waits for GPU confirmation.
Without COPY_SRC or RGBA8/BGRA8 surface format, capture returns UNSUPPORTED with
diagnostic while ordinary presentation remains available. Neither mechanism
reinterprets Coin scenes. Capture may block CPU; normal window rendering avoids it.

Historical qualified profile: synchronous RGBA8 Xlib window color in BGFX/OpenGL,
BGFX/Vulkan and wgpu/Vulkan. Depth and async tickets remain offscreen APIs; this
window API does not promise them. Capture completion is distinct from physical
visibility; see [multiple targets](coin-render-multi-target-contract.md).

Original AMD Renoir **1002:1638**, radeonsi/RADV, opaque P17 128² captures shared
FNV-1a **0xbd7999730dbe6385**. Paired no-request runs had zero staging/published
bytes. Requested BGFX published **65,536** bytes without a readback ring; wgpu
used **65,536** mapped-staging bytes released after copying. Equal checksum proves
orientation/channels for this scene, not all physical platforms. Window/Surface
tests cover current-frame capture, one-frame requests, resize/suspend/restore,
timeout rollback/consumption and surface/device recovery while keeping normal
render pixels unavailable. The original isolated-session recipe depended on
local integration tooling now archived; it is not an included host prerequisite.
Full receipts are in the [companion archive](coin-render-evidence.md).
