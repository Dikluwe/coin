#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>

#include "rendering/wgpu/SoWgpuRenderTargetP.h"
#include "rendering/wgpu/coin_wgpu_ffi.h"

#if defined(HAVE_WGPU_RUST_BRIDGE)
#include "rendering/wgpu/SoWgpuRustBackend.h"
#endif

#if defined(__linux__) || defined(__unix__)
#include <unistd.h>
#endif
#include <chrono>
#include <thread>

#if defined(HAVE_X11)
#include <X11/Xlib.h>
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
    return 1; \
  } \
} while (0)

int main(int argc, char ** argv) {
  bool requireDisplay = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--require-display") == 0) {
      requireDisplay = true;
    }
  }
  if (const char * envReq = std::getenv("COIN_TEST_REQUIRE_DISPLAY")) {
    if (envReq[0] == '1' || envReq[0] == 'y' || envReq[0] == 'Y') {
      requireDisplay = true;
    }
  }

  SoDB::init();
  std::cout << "Running WgpuSurfaceTest..." << std::endl;

  // Build canonical cone scene
  SoSeparator * root = new SoSeparator;
  root->ref();

  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  root->addChild(camera);

  SoDirectionalLight * light = new SoDirectionalLight;
  light->direction.setValue(0.0f, 0.0f, -1.0f);
  light->color.setValue(1.0f, 1.0f, 1.0f);
  light->intensity = 1.0f;
  root->addChild(light);

  SoMaterial * mat = new SoMaterial;
  mat->diffuseColor.setValue(0.8f, 0.2f, 0.2f);
  mat->ambientColor.setValue(0.2f, 0.05f, 0.05f);
  mat->specularColor.setValue(0.9f, 0.9f, 0.9f);
  mat->shininess = 0.5f;
  root->addChild(mat);

  SoCone * cone = new SoCone;
  root->addChild(cone);

  // =========================================================================
  // Test 1: Contract Validation (Headless / Without Display)
  // =========================================================================
  std::cout << "-> Test 1: Descriptor contract validation..." << std::endl;

  // 1.1 ABI version mismatch
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = 999;
    desc.structSize = sizeof(desc);
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target != nullptr, "Target pointer must not be null");
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for ABI mismatch");
    TEST_ASSERT(std::string(target->getLastError()).find("Invalid ABI version") != std::string::npos, "Diagnostic must describe ABI mismatch");
    delete target;
  }

  // 1.2 Struct size mismatch
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc) - 4;
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for structSize mismatch");
    TEST_ASSERT(std::string(target->getLastError()).find("Invalid structSize") != std::string::npos, "Diagnostic must describe structSize mismatch");
    delete target;
  }

  // 1.3 Reserved field non-zero
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 42;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for non-zero reserved");
    TEST_ASSERT(std::string(target->getLastError()).find("Reserved field") != std::string::npos, "Diagnostic must describe reserved field error");
    delete target;
  }

  // 1.4 Unsupported tags (Wayland, Win32, AppKit)
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.reserved = 0;

    desc.type = COIN_WGPU_SURFACE_WAYLAND;
    SoWgpuRenderTarget * targetW = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetW->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Wayland must be TARGET_ERROR in 1B");
    TEST_ASSERT(std::string(targetW->getLastError()).find("Wayland") != std::string::npos, "Must mention Wayland unsupported");
    delete targetW;

    desc.type = COIN_WGPU_SURFACE_WIN32;
    SoWgpuRenderTarget * targetWin = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetWin->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Win32 must be TARGET_ERROR in 1B");
    delete targetWin;

    desc.type = COIN_WGPU_SURFACE_APPKIT_LAYER;
    SoWgpuRenderTarget * targetAppKit = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetAppKit->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "AppKit must be TARGET_ERROR in 1B");
    delete targetAppKit;
  }

  // 1.5 Null/Zero Handles for Xlib
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = nullptr;
    desc.native.xlib.window = 100;

    SoWgpuRenderTarget * targetNullDisp = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetNullDisp->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Null display must fail");
    delete targetNullDisp;

    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 0;
    SoWgpuRenderTarget * targetZeroWin = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetZeroWin->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Zero window ID must fail");
    delete targetZeroWin;
  }

  // 1.6 Negative Dimensions
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 100;

    SoWgpuRenderTarget * targetNeg = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(-10, 100));
    TEST_ASSERT(targetNeg->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Negative dimension must fail");
    delete targetNeg;
  }

  // =========================================================================
  // Test 2: Minimized / Zero-size suspension and resize coalescing
  // =========================================================================
  std::cout << "-> Test 2: Zero-size suspension and resize coalescing..." << std::endl;
  {
    SoWgpuNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_WGPU_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 100;

    SoWgpuRenderTarget * target = SoWgpuRenderTarget::createWindow(desc, SbVec2i32(0, 0));
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_NOT_READY, "Zero size must start in TARGET_NOT_READY");

    SoWgpuRenderAction action;
    action.setRenderTarget(target);
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::NOT_READY, "apply() on suspended target must return NOT_READY");

    // Coalesce resizes
    target->resize(SbVec2i32(800, 600));
    target->resize(SbVec2i32(1024, 768));
    target->resize(SbVec2i32(1920, 1080));
    TEST_ASSERT(target->getSize() == SbVec2i32(1920, 1080), "Coalesced size must match last resize");
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_READY, "Status must be TARGET_READY after valid resize");

    // Suspend again
    target->resize(SbVec2i32(0, 0));
    TEST_ASSERT(target->getStatus() == SoWgpuRenderTarget::TARGET_NOT_READY, "Resize(0, 0) must return to TARGET_NOT_READY");

    delete target;
  }

  // =========================================================================
  // Test 3: Idempotent Destruction
  // =========================================================================
#if defined(HGPU_WGPU_RUST_BRIDGE) || defined(HAVE_WGPU_RUST_BRIDGE)
  std::cout << "-> Test 3: Idempotent surface destruction FFI..." << std::endl;
  {
    char errBuf[256] = {0};
    CoinWgpuStatus st0 = coin_wgpu_surface_destroy(COIN_WGPU_INVALID_SURFACE_ID, errBuf, sizeof(errBuf));
    TEST_ASSERT(st0 == COIN_WGPU_OK, "Destroying INVALID_SURFACE_ID must be OK");

    CoinWgpuStatus stUnknown = coin_wgpu_surface_destroy(9999999, errBuf, sizeof(errBuf));
    TEST_ASSERT(stUnknown == COIN_WGPU_OK, "Destroying unknown surface ID must be OK");
  }
#endif

  // =========================================================================
  // Test 4: Real X11 Window Presentation
  // =========================================================================
#if defined(HAVE_X11) && defined(HAVE_WGPU_RUST_BRIDGE)
  const char * displayEnv = std::getenv("DISPLAY");
  if (!displayEnv || !displayEnv[0]) {
    if (requireDisplay) {
      TEST_ASSERT(false, "DISPLAY environment variable must be set when --require-display is specified");
    }
    std::cout << "DISPLAY environment variable not set, skipped X11 window presentation." << std::endl;
  } else {
    std::cout << "-> Test 4: Real X11 window presentation on DISPLAY=" << displayEnv << "..." << std::endl;

    Display * dpy = XOpenDisplay(NULL);
    if (!dpy) {
      if (requireDisplay) {
        TEST_ASSERT(false, "Could not open X11 Display (XOpenDisplay returned NULL) on required display environment");
      }
      std::cout << "WARNING: Could not open X11 Display, skipping window rendering test" << std::endl;
    } else {
      int screen = DefaultScreen(dpy);
      Window rootWin = RootWindow(dpy, screen);
      Window win = XCreateSimpleWindow(dpy, rootWin, 10, 10, 640, 480, 1,
                                       BlackPixel(dpy, screen), WhitePixel(dpy, screen));
      XSelectInput(dpy, win, ExposureMask | StructureNotifyMask);
      XMapWindow(dpy, win);
      XFlush(dpy);

      SoWgpuNativeSurfaceDescriptor nativeDesc{};
      nativeDesc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
      nativeDesc.structSize = sizeof(nativeDesc);
      nativeDesc.type = COIN_WGPU_SURFACE_XLIB;
      nativeDesc.reserved = 0;
      nativeDesc.native.xlib.display = dpy;
      nativeDesc.native.xlib.window = win;

      SoWgpuRenderTarget * windowTarget = SoWgpuRenderTarget::createWindow(nativeDesc, SbVec2i32(640, 480));
      TEST_ASSERT(windowTarget->getStatus() == SoWgpuRenderTarget::TARGET_READY, "Window target must be TARGET_READY");

      SoWgpuRenderAction action;
      action.setRenderTarget(windowTarget);
      action.setViewportRegion(SbViewportRegion(640, 480));

      // Present multiple frames
      for (int f = 0; f < 5; ++f) {
        action.apply(root);
        TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Window render apply must succeed");
      }

      // Test Resize
      XResizeWindow(dpy, win, 800, 600);
      XFlush(dpy);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      windowTarget->resize(SbVec2i32(800, 600));
      action.setViewportRegion(SbViewportRegion(800, 600));
      action.apply(root);
      if (action.getLastStatus() != SoWgpuRenderAction::SUCCESS) {
        std::cerr << "RESIZE FAILED with status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render after resize must succeed");

      // Test Minimization / Suspension
      windowTarget->resize(SbVec2i32(0, 0));
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::NOT_READY, "Minimization must return NOT_READY");

      // Test Restoration
      XResizeWindow(dpy, win, 640, 480);
      XFlush(dpy);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      windowTarget->resize(SbVec2i32(640, 480));
      action.setViewportRegion(SbViewportRegion(640, 480));
      action.apply(root);
      if (action.getLastStatus() != SoWgpuRenderAction::SUCCESS) {
        std::cerr << "RESTORATION FAILED with status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Render after restoration must succeed");

      // =====================================================================
      // Test 5: Deterministic Fault Injection on Active Surface
      // =====================================================================
      std::cout << "-> Test 5: Deterministic fault injections on active surface..." << std::endl;

      // 5.1 Timeout -> NOT_READY
      coin_wgpu_inject_fault(101); // FAULT_SURFACE_TIMEOUT
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::NOT_READY, "Surface timeout must yield NOT_READY");
      coin_wgpu_inject_fault(0);

      // Subsequent frame succeeds
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Next frame after timeout must succeed");

      // 5.2 Outdated Once -> Automatically recovers in same frame -> SUCCESS
      coin_wgpu_inject_fault(102); // FAULT_SURFACE_OUTDATED_ONCE
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Outdated once must recover and succeed");
      coin_wgpu_inject_fault(0);

      // 5.3 Lost Once -> Automatically recreates and recovers in same frame -> SUCCESS
      coin_wgpu_inject_fault(103); // FAULT_SURFACE_LOST_ONCE
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Lost once must recover and succeed");
      coin_wgpu_inject_fault(0);

      // 5.4 Persistent Lost -> SURFACE_LOST after 1 retry
      coin_wgpu_inject_fault(104); // FAULT_SURFACE_LOST_PERSISTENT
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SURFACE_LOST, "Persistent lost must report SURFACE_LOST");
      TEST_ASSERT(windowTarget->getStatus() == SoWgpuRenderTarget::TARGET_SURFACE_LOST, "Target must be TARGET_SURFACE_LOST");
      coin_wgpu_inject_fault(0);

      // 5.5 Next apply after persistent lost attempts recreation
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Target must recover on subsequent frame after persistent lost cleared");

      // 5.6 Device Lost recovery while window surface is active:
      // Surface must survive device loss and reconfigure for new device generation
      std::cout << "-> Test 5.6: Window surface survival across DEVICE_LOST..." << std::endl;
      coin_wgpu_inject_fault(5); // COIN_WGPU_DEVICE_LOST
      action.apply(root);
      if (action.getLastStatus() != SoWgpuRenderAction::DEVICE_LOST) {
        std::cerr << "DEVICE_LOST test got status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::DEVICE_LOST, "Must report DEVICE_LOST");
      coin_wgpu_inject_fault(0);

      // Next apply must recreate device and reconfigure surface seamlessly
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::SUCCESS, "Must seamlessly recover after DEVICE_LOST");

      // 5.7 Out of Memory -> OUT_OF_MEMORY and transition to TARGET_ERROR
      coin_wgpu_inject_fault(105); // FAULT_SURFACE_OUT_OF_MEMORY
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::OUT_OF_MEMORY, "Surface OOM must report OUT_OF_MEMORY");
      TEST_ASSERT(windowTarget->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Target must transition to TARGET_ERROR on OOM");
      TEST_ASSERT(windowTarget->getLastError() != nullptr && windowTarget->getLastError()[0] != '\0', "Target getLastError() must have diagnostic on OOM");
      coin_wgpu_inject_fault(0);

      // Subsequent apply() on TARGET_ERROR must report BACKEND_ERROR (fatal, not NOT_READY)
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::BACKEND_ERROR, "Subsequent apply on TARGET_ERROR must report BACKEND_ERROR");

      // 5.8 Other error -> BACKEND_ERROR on new target
      SoWgpuRenderTarget * windowTarget2 = SoWgpuRenderTarget::createWindow(nativeDesc, SbVec2i32(640, 480));
      action.setRenderTarget(windowTarget2);
      coin_wgpu_inject_fault(106); // FAULT_SURFACE_OTHER
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == SoWgpuRenderAction::BACKEND_ERROR, "Surface other error must report BACKEND_ERROR");
      TEST_ASSERT(windowTarget2->getStatus() == SoWgpuRenderTarget::TARGET_ERROR, "Target must transition to TARGET_ERROR on OTHER");
      coin_wgpu_inject_fault(0);
      delete windowTarget2;

      // Clean up target and X11 window
      delete windowTarget;
      XDestroyWindow(dpy, win);
      XCloseDisplay(dpy);
      std::cout << "-> X11 Window presentation and fault injection tests PASSED!" << std::endl;
    }
  }
#else
  if (requireDisplay) {
    TEST_ASSERT(false, "X11 and Rust Bridge support required for --require-display");
  }
  std::cout << "X11 or Rust Bridge not available, skipped X11 window presentation." << std::endl;
#endif

  root->unref();
  std::cout << "ALL WgpuSurfaceTest checks PASSED successfully!" << std::endl;
  return 0;
}
