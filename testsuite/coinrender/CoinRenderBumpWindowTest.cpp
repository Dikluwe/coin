#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

static unsigned char channel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { mask >>= 1; value >>= 1; }
  return static_cast<unsigned char>(((value & mask) * 255 + mask / 2) / mask);
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  Display * display = XOpenDisplay(nullptr);
  if (!display) return 77;
  int width = 256, height = 256;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      32, 32, width, height, 0, 0, 0);
  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
  XMapRaised(display, window); XSync(display, False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display = display;
  native.native.xlib.window = window;
  CoinRenderCapabilities caps{};
  if(coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      &caps,sizeof(caps))!=0 || !caps.gpu_available ||
     caps.renderer==COIN_RENDER_RENDERER_UNKNOWN) return 77;
  const auto requestedRenderer=static_cast<CoinRenderRenderer>(caps.renderer);
  std::printf("bump_window backend=%u renderer=%u vendor=0x%x adapter=%s\n",
    unsigned(caps.backend),unsigned(caps.renderer),unsigned(caps.vendor_id),caps.adapter_name);
  CoinRenderOptions initialOptions;
  initialOptions.renderer=requestedRenderer;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(
    native, SbVec2i32(width, height),initialOptions));
  if (!target || target->getStatus() != CoinRenderTarget::TARGET_READY ||
      target->getOptions().renderer!=requestedRenderer) return 1;

  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0,0,4); camera->height = 4;
  root->addChild(camera);
  auto * light = new SoDirectionalLight;
  light->direction.setValue(0,0,-1);
  root->addChild(light);
  auto * hints = new SoShapeHints;
  hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
  root->addChild(hints);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.8f,.2f,.1f);
  root->addChild(material);
  auto * bump = new SoBumpMap;
  const unsigned char normal[] = {128,128,255};
  bump->image.setValue(SbVec2s(1,1),3,normal);
  root->addChild(bump);
  root->addChild(new SoCube);
  CoinRenderAction action(SbViewportRegion(width,height));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setRenderTarget(target.get());

  auto observe = [&](unsigned char red, unsigned char green, unsigned char blue,
                     int tolerance = 0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      XSync(display, False);
      XImage * image = XGetImage(display, window, width/2, height/2, 1, 1,
                                AllPlanes, ZPixmap);
      if (image) {
        const unsigned long pixel = XGetPixel(image,0,0);
        const unsigned char observedRed = channel(pixel,image->red_mask);
        const unsigned char observedGreen = channel(pixel,image->green_mask);
        const unsigned char observedBlue = channel(pixel,image->blue_mask);
        XDestroyImage(image);
        if (std::abs(int(observedRed)-int(red)) <= tolerance &&
            std::abs(int(observedGreen)-int(green)) <= tolerance &&
            std::abs(int(observedBlue)-int(blue)) <= tolerance) return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      !target->getLastSubmissionSerial() || !observe(204,51,25)) {
    std::fprintf(stderr,"R26 window bump failed: %s\n",action.getLastError().getString());
    return 1;
  }
  const uint64_t firstSerial = target->getLastSubmissionSerial();
  auto * base = new SoTexture2;
  const unsigned char basePixels[] = {128,128,128,255};
  base->image.setValue(SbVec2s(1,1),4,basePixels);
  base->model = SoTexture2::MODULATE;
  root->insertChild(base,4);
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      target->getLastSubmissionSerial() <= firstSerial || !observe(102,26,13)) {
    std::fprintf(stderr,"R26 textured window bump failed: %s\n",action.getLastError().getString());
    return 1;
  }
  const uint64_t texturedSerial = target->getLastSubmissionSerial();
  width = 320; height = 192;
  XResizeWindow(display, window, width, height); XSync(display, False);
  XWindowAttributes actual{};
  const auto resizeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  do {
    XGetWindowAttributes(display,window,&actual);
    if (actual.width == width && actual.height == height) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < resizeDeadline);
  if (actual.width != width || actual.height != height ||
      !target->resize(SbVec2i32(width,height))) {
    std::fprintf(stderr,"R26 window bump native resize failed\n"); return 1;
  }
  action.setViewportRegion(SbViewportRegion(width,height));
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      target->getLastSubmissionSerial() <= texturedSerial || !observe(102,26,13)) {
    std::fprintf(stderr,"R26 window bump resize failed: %s\n",action.getLastError().getString());
    return 1;
  }
  const uint64_t resizedSerial = target->getLastSubmissionSerial();
  const unsigned char translucentPixels[] = {128,128,128,128};
  base->image.setValue(SbVec2s(1,1),4,translucentPixels);
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::UNSUPPORTED ||
      target->getLastSubmissionSerial() != resizedSerial || !observe(102,26,13)) {
    std::fprintf(stderr,"R26 window bump alpha rejection changed presentation\n");
    return 1;
  }
  base->image.setValue(SbVec2s(1,1),4,basePixels);
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      target->getLastSubmissionSerial() <= resizedSerial || !observe(102,26,13)) {
    std::fprintf(stderr,"R26 window bump alpha recovery failed: %s\n",action.getLastError().getString());
    return 1;
  }
  std::printf("R26 window bump PASS: plain=[204,51,25] textured=[102,26,13] resized=320x192 alpha-recovery=1 serial=%llu\n",
    static_cast<unsigned long long>(target->getLastSubmissionSerial()));
  action.setRenderTarget(nullptr); target.reset();
  CoinRenderOptions srgbOptions;
  srgbOptions.renderer=requestedRenderer;
  srgbOptions.storedTextureColorSpace = COIN_RENDER_TEXTURE_SRGB;
  target.reset(CoinRenderTarget::createWindow(native, SbVec2i32(width,height), srgbOptions));
  if (!target || target->getStatus() != CoinRenderTarget::TARGET_READY ||
      target->getOptions().renderer!=requestedRenderer) return 1;
  action.setRenderTarget(target.get());
  action.apply(root);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      !target->getLastSubmissionSerial() || !observe(44,11,6,1)) {
    std::fprintf(stderr,"R26 sRGB window bump failed: %s\n",action.getLastError().getString());
    return 1;
  }
  std::printf("R26 sRGB window bump PASS: center=[44,11,6] tolerance=1 serial=%llu\n",
    static_cast<unsigned long long>(target->getLastSubmissionSerial()));
  action.setRenderTarget(nullptr);
  root->unref(); target.reset();
  XDestroyWindow(display,window); XCloseDisplay(display);
  return 0;
}
