# Multiple targets and recovery (P14)

Coin actions have their own viewport/state. CoinGL offscreen rendering activates
its canvas/cache context and restores the previous context; another canvas does
not silently replace its image. CoinRender retains this isolation with owner/
generation stamps and [transactional publication](coin-render-rtt-publication-contract.md).
Wiring coordinates ordered actions, target lifecycle, lost peers and publication;
Core owns queue admission and stamps; Infra owns fences/views/textures/buffers and
safe retirement/recreation; Shell owns options/diagnostics.

## Scheduling and limits

The caller orders apply/applyAsync per target. RTT producers precede consumers.
Tickets retain submitted frame/dimensions independently of later resize/destruction.
Producer mutation/another target cannot publish into this target.

BGFX has one process runtime, one active renderer and one API thread. A prepared
target starts with **16 views**, reserving further contiguous blocks for larger
compositions. Capacity depends on `caps.maxViews`, live peers and contiguous free
spans. Destruction returns all blocks; exhaustion returns `UNSUPPORTED`. B51
expanded the old fixed-16-per-frame limit, with B51.2 resource-boundary qualification
in four BGFX Linux cells. Preflight observes pool without reserving: DIRECT checks
largest shared root/producer span; STAGED needs distinct contiguous parent and
largest sequential-child spans, respecting live peers without summing destroyed
children. Reject before first producer, preserving RGBA/depth/serial; reserve on
submission. STAGED keeps its consumer prepared while children live, preventing
runtime shutdown between passes. Destroying the initializer does not stop peers.

Public wgpu targets use default device. Extra-device private FFI tests do not
add public device selection; BGFX does not gain multiple devices.

Linux Xlib/Vulkan host owns Display/window and may close/destroy them after target
destruction while peers retain runtime. Pointer reuse is not proof a new connection
was prepared. BGFX and wgpu Vulkan-Xlib HAL negotiate XFIXES on each concrete
connection before WSI, requiring version **≥2** and a valid response to request
**6.0**. Missing/invalid responses reject with diagnostic before publication while
preserving offscreen peers. OpenGL/offscreen targets do not inherit this window
prerequisite. B54 qualification does not close B34 recreation failures.

The common queue admits **16 tickets and 128 MiB packed pending output**, color+
depth, with checked arithmetic. Action checks before RTT producers; Target before
root submission; BGFX defends executor entry too. Rust concrete job occupancy
includes cancelled work pending retirement and aggregates its runtime devices.
Lost descriptors without staging consume no output bytes. Admission observes
occupancy, not a concurrent reservation. Caller-ordered Coin applications are
required; no graph mutation/traversal or same-target thread safety is promised.
BGFX polling/cancel/destruction also require its API thread. Staging alignment,
caches and driver memory are separate; 128 MiB is not total GPU memory.

## Shared loss and reconstruction

BGFX loss invalidates the shared device. Wiring retires all registered connectors,
advances target generations and marks `TARGET_LOST`. Infra holds readback buffers
until shutdown, releases references and retains loss descriptors only. Old tickets
return `READBACK_DEVICE_LOST` preserving vectors and can be cancelled; they do not
block next-generation recreation. Next apply on an existing target recreates it
with a new runtime epoch.

wgpu drops lost-generation resources/recreates default device; peers rebuild on
next execution. CoinRender observes prepared/completed domains and advances target
resource generation for changed epochs. Old tickets report loss/invalidity without
publication. Wrong-owner/epoch RTT tokens remain rejected. Epoch change between
producers rejects graph and frees temporaries without assuming the new healthy
device must also be destroyed.

Loss preserves last CPU color/depth, serial, validated revision and borrowed
allocations. Validated revision belongs to captured plan, not GPU handles; resize
still invalidates it. Later success replaces results atomically. Configuration,
OOM or generic backend errors do not gain infinite retries; explicit target
recovery/status policy still applies.

## Host contexts and presentation timing

Linux BGFX/OpenGL owns a private X11 connection to first-target/default server.
It does not own caller EGL resources: destroying all targets must preserve caller
context/pbuffer/unbind/rebind. Private connection lasts until actual BGFX shutdown,
including async tickets outliving targets, then closes. Host window remains caller-
owned; creation is synchronized before another connection uses its XID. This
avoids assuming eglInitialize adds an independent ownership reference on an
already initialized EGLDisplay. B61 context preservation does not resolve B34.

Explicit BGFX window readback waits for the current scene callback. `frame()`
alone synchronizes the prior frame, so B63 uses an empty FLUSH frame as fence
without repeating scene or presenting the empty frame. Cold preparation/drawing
stay inside apply; callback deadline is unchanged; serial advances once. Capture
completion and physical presentation remain distinct: callback can precede flip,
and Vulkan queued present does not prove X11 visibility. No promise that first
XGetImage after apply sees the new frame. Physical tests observe within their
existing **5-second** deadline, without reapplying, and compare full framebuffer
against same-apply readback plus color/MSAA oracles. This observer deadline adds
no fixed product wait and does not close B34/B94/B95.

## Evidence scope

`CoinRenderMultiTargetTest` covers staged/direct wgpu and BGFX Vulkan/OpenGL,
interleaving, resize isolation, tickets surviving target destruction, queue/byte/
overflow boundaries, seventeenth-ticket rejection before producers, cancellation,
views exhaustion/return, pending-ticket loss and same-target recovery. Serial/
armed faults verify pre-producer rejection. MultiWindow adds suspension, initializer
swap/destruction and shared loss with opaque/OIT/peeling peers.

Historical closure used C++11 BGFX/CPU and Debug wgpu: Rust **16+2**, CPU **4/4**,
wgpu **33/33**, final pending-ticket-loss controls **2/2 wgpu, 4/4 BGFX**; broad BGFX
**74** cases and final affected **18/18**. Initial epoch regressions were fixed;
two OpenGL-capability failures passed in fresh Xvfb but remain recorded as
intermittency, not driver stability. Full original receipts are
[archived](coin-render-evidence.md). Physical drivers/platforms, host integrations,
expanded RTT formats and total physical memory retain separate qualification.
