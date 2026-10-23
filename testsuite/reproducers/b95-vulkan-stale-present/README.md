# B95: Vulkan/Xlib stale presentation

Manual Linux diagnostic without Coin, CoinRender, BGFX or wgpu linkage. B95
tracks a stale X11 window image after successful private-resolve and swapchain
readbacks, completed submission fence and successful present. The responsible
WSI/driver/compositor component and occurrence frequency remain undetermined;
B34 and B94 are separate issues. A passing control does not establish a fix.

Requires Vulkan 1.3, Xlib, desktop OpenGL, runtime libEGL and Threads. It selects
actual NVIDIA Vulkan vendor `0x10de`; the EGL reference also requires NVIDIA
and a compatibility context. The observed setup was private headless Weston/
Xwayland rendered by AMD, with the probe client using NVIDIA PRIME render
offload. Use an isolated, accelerated X11 session and its `DISPLAY`/`XAUTHORITY`;
NVIDIA need not be the host's primary provider. The probe owns its windows and
connections. It does not change DPMS, RandR, host providers, driver or session
configuration.

Build independently, with all build products in RAM:

```sh
b95_build="$(mktemp -d /dev/shm/coin-b95.XXXXXX)"
cmake -S testsuite/reproducers/b95-vulkan-stale-present -B "$b95_build" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$b95_build" -j2
```

Apply NVIDIA selection only to the client in that private X11 session, leaving
Weston/Xwayland and the host provider unchanged. Adjust the installed driver JSON
paths if needed. Compare two versus three requested swapchain images in fresh
processes, keeping all other inputs identical:

```sh
b95_nvidia_icd=/usr/share/vulkan/icd.d/nvidia_icd.json
b95_nvidia_egl=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
b95_client() {
  env VK_DRIVER_FILES="$b95_nvidia_icd" \
    __EGL_VENDOR_LIBRARY_FILENAMES="$b95_nvidia_egl" \
    __GLX_VENDOR_LIBRARY_NAME=nvidia \
    __NV_PRIME_RENDER_OFFLOAD=1 \
    __VK_LAYER_NV_optimus=NVIDIA_only \
    "$b95_build/B95VulkanStalePresent" "$@"
}
b95_client --frames 96 --override-redirect --async-present --repeat-color 4 --present-mode immediate --images 2
b95_client --frames 96 --override-redirect --async-present --repeat-color 4 --present-mode immediate --images 3
```

`--images` is Vulkan's requested **minimum** image count. Unsupported requests
fail without clamping; inspect `requested_images` and `actual_images` before
interpreting a two/three-image comparison. `--validation` requires the installed
`VK_LAYER_KHRONOS_validation`; synchronization validation must also be enabled
in that layer's configuration. No validation layer is downloaded or skipped.

Every frame clears a private MSAA4 attachment, resolves to 1x, copies to the
acquired swapchain image and verifies both images before its single present.
Submission/acquisition waits are 1 second. XGetImage observes exact expected RGB
for at most 5 seconds, without another render. The sequence resizes 96 to 128
halfway through. EGL MSAA4/FBO/readPixels/unbind runs after native rendering;
`--async-present` permits it and XGetImage to overlap the sole present worker.
The worker is joined before any subsequent Vulkan use or teardown. Logs include
monotonic timestamps, handles, serials, actual modes/counts and observed colors.

Defaults remain 96 frames, FIFO, three requested images, distinct RGB per frame,
synchronous present and EGL interleave. `--repeat-color N` changes RGB only after
N complete publications; EGL colors remain distinct. `--no-egl` omits its context.
`--swapchain-maintenance1` separately requires the feature/extensions, announces
compatible FIFO/IMMEDIATE modes and explicitly selects the requested mode in each
present. It has no fallback and is not evidence of a remedy.

Exit 0 means every requested frame and normal teardown passed; 1 reports an API,
capability or GPU readback error; 3 means physical RGB was not observed in 5 seconds;
4 reports validation/teardown errors and takes precedence over other failure codes,
including stale physical RGB or an API error; 64 is invalid input. Signals and
hangs retain their process outcome; a watchdog must never turn them into success.
This program is not registered in the mandatory test suite.
