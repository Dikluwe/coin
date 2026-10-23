# B95: stock wgpu FIFO stale presentation

Manual Linux/Xlib diagnostic using registry wgpu 24.0.5, raw-window-handle 0.6.2,
pollster 0.4 and an observable logger. No Coin, bridge, vendor patch, EGL or
mandatory test-suite registration. It does not identify whether upstream
wgpu/HAL, Vulkan WSI, driver or compositor is responsible, or establish a common
cause with the separate Vulkan B95 probe. No occurrence frequency is estimated.

Build a private source copy and all products in RAM. The local Cargo cache must
already contain the registry dependencies; offline failure is not a skipped PASS.
Requires Rust, a C++17 compiler, ar and Xlib development files.

```sh
b95_build="$(mktemp -d /dev/shm/coin-b95-wgpu.XXXXXX)"
cp -R testsuite/reproducers/b95-wgpu-stale-present "$b95_build/source"
(
  cd "$b95_build/source"
  CARGO_TARGET_DIR="$b95_build/target" TMPDIR="$b95_build" \
    CARGO_PROFILE_DEV_DEBUG=0 CARGO_INCREMENTAL=0 CARGO_BUILD_JOBS=2 \
    cargo build --offline --locked
  cargo tree --offline --locked -i wgpu-hal
)
b95_bin="$b95_build/target/debug/B95WgpuStalePresent"
```

Build outside the Coin workspace so its vendor configuration is not inherited.
Use the versioned Cargo.lock when comparing runs of the same registry baseline.
The executable selects actual NVIDIA vendor `0x10de` and rejects CPU/software
adapters. Use an isolated accelerated X11 session and its DISPLAY/XAUTHORITY.
The motivating configuration used private headless Weston/Xwayland on AMD with
only the NVIDIA client offloaded; changing the host provider is unnecessary.
The program does not change DPMS, RandR, drivers or session configuration.

Apply environment only to the client, adjusting installed JSON paths as needed:

```sh
b95_nvidia_icd=/usr/share/vulkan/icd.d/nvidia_icd.json
b95_client() {
  env VK_DRIVER_FILES="$b95_nvidia_icd" \
    __GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1 \
    __VK_LAYER_NV_optimus=NVIDIA_only \
    VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT \
    "$b95_bin" "$@"
}
b95_client --frames 256 --repeat 1
b95_client --frames 256 --repeat 2
```

Repeat 1 is the changing-scene input control; repeat 2 keeps pairs identical.
A passing input does not establish a fix. Defaults are 256 frames and repeat 2;
these are the only parameters. For loader confirmation, run
`VK_LOADER_DEBUG=layer b95_client --frames 9 --repeat 2` and inspect loading of
VK_LAYER_KHRONOS_validation. The probe additionally requires its loaded library,
validation-features capability and live Khronos/synchronization startup callback.
InstanceFlags::VALIDATION and DEBUG are explicit in release builds. WARN/ERROR
HAL messages are printed; logger ERROR and uncaptured errors are counted through
normal teardown. No layer is downloaded, simulated or silently disabled.

The override-redirect window starts at (32,32), 96x96, and resizes to 128x128 at
frame 8. The surface requests FIFO, desired maximum frame latency 2, linear
RGBA8/BGRA8 and RENDER_ATTACHMENT|COPY_SRC. Actual swapchain image count is **not
observed** by this stock probe; latency 2 is not proof of three images.
Each epoch cycles blue with red center, solid blue, then black with yellow center.
The integer inset is `(side+5)/10`. A real pipeline and scissored draw render the
center at 1x. Scene and surface capture are two command buffers in one submit.
MAP_READ, poll(Wait), completion callback and exact all-pixel GPU RGB validation
precede the single present. Only then XGetImage observes exact analytic RGB for
up to five seconds, without another render, acquire, configure or device poll.
Histories/fingerprints are logging only and are computed after observation.

Exit 0 means all frames and teardown passed; 1 is API/capability failure; 2 is an
invalid GPU analytic/completion result; 3 means GPU was exact but physical RGB was
not observed in five seconds; 4 is missing real validation evidence, logger/
uncaptured/X11 error, panic or teardown failure; 64 is invalid input. Validation
and teardown errors always override physical timeout. Counters are checked after
run resources have dropped. Signals and watchdog timeouts must retain their
failure status. Physical timeout alone does not locate the responsible component.
