#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/C/basic.h>
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCone.h>

#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinwgpu/CoinWgpuFfi.h"

#if defined(HAVE_COIN_WGPU_RUST_BRIDGE)
#include "rendering/coinwgpu/CoinWgpuBackend.h"
#endif

#if defined(__linux__) || defined(__unix__)
#include <unistd.h>
#endif
#include <chrono>
#include <thread>

#if defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
    return 1; \
  } \
} while (0)

#if defined(HAVE_X11) && defined(HAVE_COIN_WGPU_RUST_BRIDGE)
// Executed with --camera-only in a fresh process to isolate the native
// ownership and camera readmission contract.
static int nativePhongCamera(Display * dpy, bool requireVulkan)
{
  const int screen = DefaultScreen(dpy);
  const Window rootWin = RootWindow(dpy,screen);
  // Direct private transport proves native camera patches use the same
  // owned immutable geometry as offscreen submissions. Null input pointers
  // are accepted only while this device still owns the validated base.
  std::cout << "-> Native PHONG camera payload reuse..." << std::endl;
  // Use a mapped native window with an explicit, verified drawable extent.
  Window patchWin = XCreateSimpleWindow(dpy,rootWin,30,30,640,480,1,
    BlackPixel(dpy,screen),WhitePixel(dpy,screen));
  XSizeHints patchHints{};
  patchHints.flags = PMinSize | PMaxSize;
  patchHints.min_width = patchHints.max_width = 640;
  patchHints.min_height = patchHints.max_height = 480;
  XSetWMNormalHints(dpy,patchWin,&patchHints);
  XSelectInput(dpy,patchWin,StructureNotifyMask);
  XMapWindow(dpy,patchWin);
  XFlush(dpy);
  XEvent patchEvent;
  do { XWindowEvent(dpy,patchWin,StructureNotifyMask,&patchEvent); }
  while (patchEvent.type != MapNotify);
  XSync(dpy,False);
  XWindowAttributes patchAttributes{};
  TEST_ASSERT(XGetWindowAttributes(dpy,patchWin,&patchAttributes) &&
              patchAttributes.width == 640 && patchAttributes.height == 480 &&
              patchAttributes.map_state == IsViewable,
              "Native camera fixture must have a mapped drawable with matching extent");
  CoinWgpuSurfaceCreateInfo patchInfo{};
  patchInfo.abi_version = COIN_WGPU_ABI_VERSION;
  patchInfo.struct_size = sizeof(patchInfo);
  patchInfo.native.abi_version = COIN_WGPU_ABI_VERSION;
  patchInfo.native.struct_size = sizeof(patchInfo.native);
  patchInfo.native.type = COIN_WGPU_NATIVE_XLIB;
  patchInfo.native.handle_a = reinterpret_cast<uintptr_t>(dpy);
  patchInfo.native.handle_b = patchWin;
  patchInfo.width = 640; patchInfo.height = 480;
  if (requireVulkan) patchInfo.renderer = COIN_RENDER_RENDERER_VULKAN;
  CoinWgpuSurfaceId patchSurface = COIN_WGPU_INVALID_SURFACE_ID;
  char patchError[512] = {};
  const auto patchStatus = [&](CoinWgpuStatus status, const char * operation,
                                CoinWgpuStatus expectedStatus = COIN_WGPU_OK) {
    if (status != expectedStatus) {
      std::cerr << "Native PHONG " << operation << " status=" << int(status)
                << " expected=" << int(expectedStatus) << " error=" << patchError << '\n';
    }
    return status == expectedStatus;
  };
  TEST_ASSERT(patchStatus(coin_wgpu_surface_create(&patchInfo,&patchSurface,patchError,sizeof(patchError)),"surface create"),
              "Native camera test surface creation");
  CoinWgpuVertex vertices[3] = {};
  vertices[0].position[0] = -.75f; vertices[0].position[1] = -.75f;
  vertices[1].position[0] = .75f; vertices[1].position[1] = -.75f;
  vertices[2].position[1] = .75f;
  for (auto & vertex : vertices) { vertex.normal[2] = 1; vertex.screen_space_w = 1; }
  const uint32_t indices[3] = {0,1,2};
  CoinWgpuDraw draw{}; draw.vertex_count = draw.index_count = 3;
  CoinWgpuMaterial material{};
  material.diffuse[0] = .8f; material.diffuse[1] = .25f; material.diffuse[3] = 1;
  material.specular[0] = .2f; material.shininess = .25f;
  CoinWgpuRenderState state{};
  state.light_model = 1; state.light_count = 1; state.polygon_offset_primitive_style = 1;
  state.lights[0].position_type[2] = 3; state.lights[0].position_type[3] = 1;
  for (int c = 0; c < 4; ++c) {
    state.lights[0].color_intensity[c] = 1;
    state.model_view[c*5] = state.model_view_projection[c*5] = state.normal_matrix[c*5] = 1;
  }
  state.lights[0].attenuation_exponent[2] = 1;
  const CoinWgpuRenderState baseState = state;
  CoinWgpuFrameView patchFrame{};
  patchFrame.abi_version = COIN_WGPU_ABI_VERSION;
  patchFrame.struct_size = sizeof(patchFrame);
  const uint64_t initialRevision = 53001;
  const uint64_t referenceRevision = 53002;
  const uint64_t readmittedRevision = 53003;
  const uint64_t patchRevision = 53004;
  const uint64_t staleRevision = 53005;
  patchFrame.frame_revision = initialRevision; patchFrame.width = 640; patchFrame.height = 480;
  patchFrame.vertices = vertices; patchFrame.vertex_count = 3;
  patchFrame.indices = indices; patchFrame.index_count = 3;
  patchFrame.draws = &draw; patchFrame.draw_count = 1;
  patchFrame.materials = &material; patchFrame.material_count = 1;
  patchFrame.states = &state; patchFrame.state_count = 1; patchFrame.clear_color[3] = 1;
  std::vector<uint8_t> expected(640u*480u*4u,37), basePixels(expected), actual(expected);
  TEST_ASSERT(patchStatus(coin_wgpu_surface_submit_readback(patchSurface,&patchFrame,basePixels.data(),basePixels.size(),
                patchError,sizeof(patchError)),"initial immutable base"), "Native PHONG initial immutable base");
  const uint64_t firstPublished = coin_wgpu_surface_submission_serial(patchSurface);
  TEST_ASSERT(firstPublished > 0 && coin_wgpu_surface_submission_serial(0) == 0,
              "Surface publication serial belongs to the live surface");
  // A new full revision without a camera hint suspends owned snapshots.
  // Capture the reference through this full path, then explicitly readmit
  // the original camera using complete buffers before testing a null-payload patch.
  state.model_view[12] = state.model_view_projection[12] = .2f;
  state.lights[0].position_type[0] = 1;
  patchFrame.frame_revision = referenceRevision;
  TEST_ASSERT(patchStatus(coin_wgpu_surface_submit_readback(patchSurface,&patchFrame,expected.data(),expected.size(),
                patchError,sizeof(patchError)),"full camera reference"), "Native PHONG full camera reference");
  state = baseState;
  patchFrame.frame_revision = readmittedRevision;
  patchFrame.camera_base_revision = referenceRevision;
  std::vector<uint8_t> readmitted(basePixels.size(),37);
  TEST_ASSERT(patchStatus(coin_wgpu_surface_submit_readback(patchSurface,&patchFrame,readmitted.data(),readmitted.size(),
                patchError,sizeof(patchError)),"camera base readmission") && readmitted == basePixels,
              "Native PHONG readmission must fully validate buffers and restore the base image");
  state.model_view[12] = state.model_view_projection[12] = .2f;
  state.lights[0].position_type[0] = 1;
  patchFrame.frame_revision = patchRevision; patchFrame.camera_base_revision = readmittedRevision;
  patchFrame.vertices = nullptr; patchFrame.indices = nullptr;
  patchFrame.draws = nullptr; patchFrame.materials = nullptr;
  TEST_ASSERT(patchStatus(coin_wgpu_surface_submit_readback(patchSurface,&patchFrame,actual.data(),actual.size(),
                patchError,sizeof(patchError)),"camera patch") && actual == expected && actual != basePixels,
              "Native PHONG patch must use owned geometry and resolved light coordinates");
  CoinWgpuCacheStats patchStats{}; coin_wgpu_get_cache_stats(&patchStats);
  TEST_ASSERT(patchStats.frame_uploads == 0 && patchStats.frame_uploaded_bytes == 0 && patchStats.frame_hits >= 1,
              "Native PHONG patch must reuse GPU geometry and material payload");
  const uint64_t publishedBeforeReject = coin_wgpu_surface_submission_serial(patchSurface);
  TEST_ASSERT(publishedBeforeReject > firstPublished, "Surface publication serial advances on success");
  patchFrame.frame_revision = staleRevision; patchFrame.camera_base_revision = readmittedRevision;
  std::vector<uint8_t> rejected(actual.size(),37);
  TEST_ASSERT(patchStatus(coin_wgpu_surface_submit_readback(patchSurface,&patchFrame,rejected.data(),rejected.size(),
                patchError,sizeof(patchError)),"stale base",COIN_WGPU_INVALID_ARGUMENT) &&
              rejected == std::vector<uint8_t>(rejected.size(),37),
              "Stale native camera base must validate pointers and preserve the output");
  TEST_ASSERT(coin_wgpu_surface_submission_serial(patchSurface) == publishedBeforeReject,
              "Rejected surface candidate preserves its publication serial");
  TEST_ASSERT(coin_wgpu_surface_resize(patchSurface,320,240,patchError,sizeof(patchError)) == COIN_WGPU_OK &&
              coin_wgpu_surface_submission_serial(patchSurface) == 0,
              "Resized surface has no published image until the next success");
  TEST_ASSERT(coin_wgpu_surface_destroy(patchSurface,patchError,sizeof(patchError)) == COIN_WGPU_OK,
              "Native camera test surface destruction");
  XDestroyWindow(dpy,patchWin);
  return 0;
}
#endif

int main(int argc, char ** argv) {
  bool requireDisplay = false;
  bool requireVulkan = false;
  bool cameraOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--camera-only") == 0) {
      cameraOnly = true;
      requireDisplay = true;
    } else if (std::strcmp(argv[i], "--require-display") == 0) {
      requireDisplay = true;
    } else if (std::strcmp(argv[i], "--require-vulkan") == 0) {
      requireDisplay = true;
      requireVulkan = true;
    }
  }
  if (const char * envReq = std::getenv("COIN_TEST_REQUIRE_DISPLAY")) {
    if (envReq[0] == '1' || envReq[0] == 'y' || envReq[0] == 'Y') {
      requireDisplay = true;
    }
  }

  SoDB::init();
  CoinRenderAction::initClass();
  std::cout << "Running CoinRenderSurfaceTest..." << std::endl;

  if (cameraOnly) {
#if defined(HAVE_X11) && defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    Display * display = XOpenDisplay(NULL);
    TEST_ASSERT(display != nullptr, "--camera-only requires an accessible X11 display");
    const int status = nativePhongCamera(display,requireVulkan);
    XCloseDisplay(display);
    if (status == 0) std::cout << "Native PHONG camera-only checks PASSED" << std::endl;
    return status;
#else
    TEST_ASSERT(false, "--camera-only requires X11 and Rust Bridge support");
#endif
  }

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

  {
    CoinRenderTarget * offscreen = CoinRenderTarget::createOffscreen(SbVec2i32(16, 16));
    TEST_ASSERT(!offscreen->requestWindowReadbackRGBA(),
                "Offscreen target must reject a window capture request");
    delete offscreen;
  }

  // 1.1 ABI version mismatch
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = 999;
    desc.structSize = sizeof(desc);
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    CoinRenderTarget * target = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target != nullptr, "Target pointer must not be null");
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for ABI mismatch");
    TEST_ASSERT(std::string(target->getLastError()).find("Invalid ABI version") != std::string::npos, "Diagnostic must describe ABI mismatch");
    delete target;
  }

  // 1.2 Struct size mismatch
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc) - 4;
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    CoinRenderTarget * target = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for structSize mismatch");
    TEST_ASSERT(std::string(target->getLastError()).find("Invalid structSize") != std::string::npos, "Diagnostic must describe structSize mismatch");
    delete target;
  }

  // 1.3 Reserved field non-zero
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 42;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 1;

    CoinRenderTarget * target = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_ERROR, "Status must be TARGET_ERROR for non-zero reserved");
    TEST_ASSERT(std::string(target->getLastError()).find("Reserved field") != std::string::npos, "Diagnostic must describe reserved field error");
    delete target;
  }

  // 1.4 Platform tags and descriptor validation
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.reserved = 0;

    desc.type = COIN_RENDER_SURFACE_WAYLAND;
    CoinRenderTarget * targetW = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetW->getStatus() == CoinRenderTarget::TARGET_ERROR,
                "Wayland must reject null wl_display/wl_surface");
    delete targetW;
#if defined(__linux__) && defined(HAVE_COIN_WGPU_RUST_BRIDGE)
    desc.native.wayland.display = reinterpret_cast<void *>(uintptr_t(1));
    targetW = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetW->getStatus() == CoinRenderTarget::TARGET_ERROR,
                "Wayland must reject null wl_surface");
    delete targetW;
    desc.native.wayland.surface = reinterpret_cast<void *>(uintptr_t(2));
    targetW = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetW->getStatus() == CoinRenderTarget::TARGET_READY,
                "Valid non-null Wayland descriptor must be accepted before GPU preparation");
    delete targetW;
#endif

    desc.type = COIN_RENDER_SURFACE_WIN32;
    CoinRenderTarget * targetWin = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetWin->getStatus() == CoinRenderTarget::TARGET_ERROR, "Win32 must be TARGET_ERROR in 1B");
    delete targetWin;

    desc.type = COIN_RENDER_SURFACE_ANDROID_NDK;
    CoinRenderTarget * targetAndroid = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetAndroid->getStatus() == CoinRenderTarget::TARGET_ERROR,
                "Android must reject a null ANativeWindow");
    delete targetAndroid;

    desc.type = COIN_RENDER_SURFACE_APPKIT_LAYER;
    CoinRenderTarget * targetAppKit = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetAppKit->getStatus() == CoinRenderTarget::TARGET_ERROR, "AppKit must be TARGET_ERROR in 1B");
    delete targetAppKit;
  }

  // 1.5 Null/Zero Handles for Xlib
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = nullptr;
    desc.native.xlib.window = 100;

    CoinRenderTarget * targetNullDisp = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetNullDisp->getStatus() == CoinRenderTarget::TARGET_ERROR, "Null display must fail");
    delete targetNullDisp;

    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 0;
    CoinRenderTarget * targetZeroWin = CoinRenderTarget::createWindow(desc, SbVec2i32(100, 100));
    TEST_ASSERT(targetZeroWin->getStatus() == CoinRenderTarget::TARGET_ERROR, "Zero window ID must fail");
    delete targetZeroWin;
  }

  // 1.6 Negative Dimensions
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 100;

    CoinRenderTarget * targetNeg = CoinRenderTarget::createWindow(desc, SbVec2i32(-10, 100));
    TEST_ASSERT(targetNeg->getStatus() == CoinRenderTarget::TARGET_ERROR, "Negative dimension must fail");
    delete targetNeg;
  }

  // =========================================================================
  // Test 2: Minimized / Zero-size suspension and resize coalescing
  // =========================================================================
  std::cout << "-> Test 2: Zero-size suspension and resize coalescing..." << std::endl;
  {
    CoinRenderNativeSurfaceDescriptor desc{};
    desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    desc.structSize = sizeof(desc);
    desc.type = COIN_RENDER_SURFACE_XLIB;
    desc.reserved = 0;
    desc.native.xlib.display = (void*)0x1234;
    desc.native.xlib.window = 100;

    CoinRenderTarget * target = CoinRenderTarget::createWindow(desc, SbVec2i32(0, 0));
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY, "Zero size must start in TARGET_NOT_READY");

    CoinRenderAction action;
    action.setRenderTarget(target);
    action.apply(root);
    TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY, "apply() on suspended target must return NOT_READY");

    // Coalesce resizes
    target->resize(SbVec2i32(800, 600));
    target->resize(SbVec2i32(1024, 768));
    target->resize(SbVec2i32(1920, 1080));
    TEST_ASSERT(target->getSize() == SbVec2i32(1920, 1080), "Coalesced size must match last resize");
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_READY, "Status must be TARGET_READY after valid resize");

    // Suspend again
    target->resize(SbVec2i32(0, 0));
    TEST_ASSERT(target->getStatus() == CoinRenderTarget::TARGET_NOT_READY, "Resize(0, 0) must return to TARGET_NOT_READY");

    delete target;
  }

  // =========================================================================
  // Test 3: Idempotent Destruction
  // =========================================================================
#if defined(HGPU_WGPU_RUST_BRIDGE) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  std::cout << "-> Test 3: Idempotent surface destruction FFI..." << std::endl;
  {
    char errBuf[256] = {0};
    CoinWgpuStatus st0 = coin_wgpu_surface_destroy(COIN_WGPU_INVALID_SURFACE_ID, errBuf, sizeof(errBuf));
    TEST_ASSERT(st0 == COIN_WGPU_OK, "Destroying INVALID_SURFACE_ID must be OK");

    CoinWgpuStatus stUnknown = coin_wgpu_surface_destroy(9999999, errBuf, sizeof(errBuf));
    TEST_ASSERT(stUnknown == COIN_WGPU_OK, "Destroying unknown surface ID must be OK");

    CoinWgpuSurfaceCreateInfo invalidRenderer{};
    invalidRenderer.abi_version = COIN_WGPU_ABI_VERSION;
    invalidRenderer.struct_size = sizeof(invalidRenderer);
    invalidRenderer.native.abi_version = COIN_WGPU_ABI_VERSION;
    invalidRenderer.native.struct_size = sizeof(invalidRenderer.native);
    invalidRenderer.native.type = COIN_WGPU_NATIVE_XLIB;
    invalidRenderer.width = 1;
    invalidRenderer.height = 1;
    invalidRenderer.renderer = COIN_RENDER_RENDERER_OTHER;
    CoinWgpuSurfaceId unusedSurface = COIN_WGPU_INVALID_SURFACE_ID;
    TEST_ASSERT(coin_wgpu_surface_create(&invalidRenderer, &unusedSurface, errBuf, sizeof(errBuf)) ==
                COIN_WGPU_INVALID_ARGUMENT && unusedSurface == COIN_WGPU_INVALID_SURFACE_ID,
                "Unknown explicit renderer must be rejected before native surface access");
  }
#endif

  // =========================================================================
  // Test 4: Real X11 Window Presentation
  // =========================================================================
#if defined(HAVE_X11) && defined(HAVE_COIN_WGPU_RUST_BRIDGE)
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

      CoinRenderNativeSurfaceDescriptor nativeDesc{};
      nativeDesc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
      nativeDesc.structSize = sizeof(nativeDesc);
      nativeDesc.type = COIN_RENDER_SURFACE_XLIB;
      nativeDesc.reserved = 0;
      nativeDesc.native.xlib.display = dpy;
      nativeDesc.native.xlib.window = win;

      CoinRenderOptions windowOptions;
      if (requireVulkan) windowOptions.renderer = COIN_RENDER_RENDERER_VULKAN;
      CoinRenderTarget * windowTarget = CoinRenderTarget::createWindow(nativeDesc, SbVec2i32(640, 480), windowOptions);
      TEST_ASSERT(windowTarget->getStatus() == CoinRenderTarget::TARGET_READY, "Window target must be TARGET_READY");
      TEST_ASSERT(!windowTarget->isDepthReadbackEnabled() &&
                  !windowTarget->setDepthReadbackEnabled(FALSE) &&
                  std::string(windowTarget->getLastError()).find("offscreen") != std::string::npos,
                  "Window target must reject the offscreen depth readback policy");

      CoinRenderAction action;
      action.setRenderTarget(windowTarget);
      action.setViewportRegion(SbViewportRegion(640, 480));

      // Present multiple frames
      for (int f = 0; f < 5; ++f) {
        action.apply(root);
        TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Window render apply must succeed");
      }

      if (requireVulkan) {
        CoinWgpuRuntimeCapabilities caps{};
        TEST_ASSERT(coin_wgpu_query_runtime_capabilities(&caps, sizeof(caps)) == COIN_WGPU_OK &&
                    caps.renderer == COIN_RENDER_RENDERER_VULKAN,
                    "Explicit Vulkan window request must select a Vulkan adapter");
      }

      // Explicit window capture publishes only the requested frame.
      std::vector<uint8_t> windowRgba;
      windowTarget->readbackRGBA(windowRgba);
      TEST_ASSERT(windowRgba.empty(), "Normal window render must not read pixels");
      TEST_ASSERT(windowTarget->requestWindowReadbackRGBA(), "Window capture request must succeed");
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Window capture render must succeed");
      windowTarget->readbackRGBA(windowRgba);
      TEST_ASSERT(windowRgba.size() == 640u * 480u * 4u,
                  "Window capture must publish complete RGBA8");
      const std::vector<uint8_t> capturedWindow = windowRgba;
      coin_wgpu_inject_fault(101); // timeout before acquisition
      TEST_ASSERT(windowTarget->requestWindowReadbackRGBA(), "Failed capture request must be accepted");
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY,
                  "Failed window capture must report NOT_READY");
      windowTarget->readbackRGBA(windowRgba);
      TEST_ASSERT(windowRgba == capturedWindow,
                  "Failed window capture must preserve the last published frame");
      coin_wgpu_inject_fault(0);
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS,
                  "Normal render after failed capture must recover");
      windowTarget->readbackRGBA(windowRgba);
      TEST_ASSERT(windowRgba.empty(), "Capture request must be one-shot");

      // Test Resize
      XResizeWindow(dpy, win, 800, 600);
      XFlush(dpy);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      windowTarget->resize(SbVec2i32(800, 600));
      action.setViewportRegion(SbViewportRegion(800, 600));
      action.apply(root);
      if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
        std::cerr << "RESIZE FAILED with status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Render after resize must succeed");

      // Test Minimization / Suspension
      windowTarget->resize(SbVec2i32(0, 0));
      TEST_ASSERT(!windowTarget->requestWindowReadbackRGBA(),
                  "Suspended window must reject capture request");
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY, "Minimization must return NOT_READY");

      // Test Restoration
      XResizeWindow(dpy, win, 640, 480);
      XFlush(dpy);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      windowTarget->resize(SbVec2i32(640, 480));
      action.setViewportRegion(SbViewportRegion(640, 480));
      action.apply(root);
      if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
        std::cerr << "RESTORATION FAILED with status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Render after restoration must succeed");

      // =====================================================================
      // Test 5: Deterministic Fault Injection on Active Surface
      // =====================================================================
      std::cout << "-> Test 5: Deterministic fault injections on active surface..." << std::endl;

      // 5.1 Timeout -> NOT_READY
      coin_wgpu_inject_fault(101); // FAULT_SURFACE_TIMEOUT
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::NOT_READY, "Surface timeout must yield NOT_READY");
      coin_wgpu_inject_fault(0);

      // Subsequent frame succeeds
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Next frame after timeout must succeed");

      // 5.2 Outdated Once -> Automatically recovers in same frame -> SUCCESS
      coin_wgpu_inject_fault(102); // FAULT_SURFACE_OUTDATED_ONCE
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Outdated once must recover and succeed");
      coin_wgpu_inject_fault(0);

      // 5.3 Lost Once -> Automatically recreates and recovers in same frame -> SUCCESS
      coin_wgpu_inject_fault(103); // FAULT_SURFACE_LOST_ONCE
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Lost once must recover and succeed");
      coin_wgpu_inject_fault(0);

      // 5.4 Persistent Lost -> SURFACE_LOST after 1 retry
      coin_wgpu_inject_fault(104); // FAULT_SURFACE_LOST_PERSISTENT
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SURFACE_LOST, "Persistent lost must report SURFACE_LOST");
      TEST_ASSERT(windowTarget->getStatus() == CoinRenderTarget::TARGET_SURFACE_LOST, "Target must be TARGET_SURFACE_LOST");
      coin_wgpu_inject_fault(0);

      // 5.5 Next apply after persistent lost attempts recreation
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Target must recover on subsequent frame after persistent lost cleared");

      // 5.6 Device Lost recovery while window surface is active:
      // Surface must survive device loss and reconfigure for new device generation
      std::cout << "-> Test 5.6: Window surface survival across DEVICE_LOST..." << std::endl;
      coin_wgpu_inject_fault(5); // COIN_WGPU_DEVICE_LOST
      action.apply(root);
      if (action.getLastStatus() != CoinRenderAction::DEVICE_LOST) {
        std::cerr << "DEVICE_LOST test got status=" << action.getLastStatus() << " error=" << action.getLastError().getString() << std::endl;
      }
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::DEVICE_LOST, "Must report DEVICE_LOST");
      coin_wgpu_inject_fault(0);

      // Next apply must recreate device and reconfigure surface seamlessly
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::SUCCESS, "Must seamlessly recover after DEVICE_LOST");

      // 5.7 Out of Memory -> OUT_OF_MEMORY and transition to TARGET_ERROR
      coin_wgpu_inject_fault(105); // FAULT_SURFACE_OUT_OF_MEMORY
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY, "Surface OOM must report OUT_OF_MEMORY");
      TEST_ASSERT(windowTarget->getStatus() == CoinRenderTarget::TARGET_ERROR, "Target must transition to TARGET_ERROR on OOM");
      TEST_ASSERT(windowTarget->getLastError() != nullptr && windowTarget->getLastError()[0] != '\0', "Target getLastError() must have diagnostic on OOM");
      coin_wgpu_inject_fault(0);

      // Subsequent apply() on TARGET_ERROR must report BACKEND_ERROR (fatal, not NOT_READY)
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR, "Subsequent apply on TARGET_ERROR must report BACKEND_ERROR");

      // 5.8 Other error -> BACKEND_ERROR on new target
      CoinRenderTarget * windowTarget2 = CoinRenderTarget::createWindow(nativeDesc, SbVec2i32(640, 480));
      action.setRenderTarget(windowTarget2);
      coin_wgpu_inject_fault(106); // FAULT_SURFACE_OTHER
      action.apply(root);
      TEST_ASSERT(action.getLastStatus() == CoinRenderAction::BACKEND_ERROR, "Surface other error must report BACKEND_ERROR");
      TEST_ASSERT(windowTarget2->getStatus() == CoinRenderTarget::TARGET_ERROR, "Target must transition to TARGET_ERROR on OTHER");
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
  std::cout << "ALL CoinRenderSurfaceTest checks PASSED successfully!" << std::endl;
  return 0;
}
