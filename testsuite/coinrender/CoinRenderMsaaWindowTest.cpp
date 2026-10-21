#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

struct Pixel { unsigned char r, g, b; };
static unsigned char channel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { mask >>= 1; value >>= 1; }
  return static_cast<unsigned char>(((value & mask) * 255 + mask / 2) / mask);
}
static std::vector<Pixel> capture(Display * display, Window window, int size) {
  XSync(display, False);
  XImage * image = XGetImage(display, window, 0, 0, size, size, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<Pixel> pixels(size_t(size) * size);
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const unsigned long value = XGetPixel(image, x, y);
    pixels[size_t(y) * size + x] = {channel(value, image->red_mask),
                                    channel(value, image->green_mask),
                                    channel(value, image->blue_mask)};
  }
  XDestroyImage(image);
  return pixels;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  Display * display = XOpenDisplay(nullptr);
  if (!display) return 77;
  constexpr int size = 32;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      32, 32, size, size, 0, 0, 0);
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

  SoSeparator * scene = new SoSeparator; scene->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 2); camera->height = 2;
  camera->nearDistance = 0.1f; camera->farDistance = 10;
  scene->addChild(camera);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(1, 0, 0);
  material->emissiveColor.setValue(1, 0, 0);
  scene->addChild(material);
  auto * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, -1, -1, 0);
  coordinates->point.set1Value(1, 1, -1, 0);
  coordinates->point.set1Value(2, -1, 0.83f, 0);
  scene->addChild(coordinates);
  auto * face = new SoIndexedFaceSet;
  face->coordIndex.set1Value(0, 0); face->coordIndex.set1Value(1, 1);
  face->coordIndex.set1Value(2, 2); face->coordIndex.set1Value(3, -1);
  scene->addChild(face);

  bool passed = true;
  for (uint32_t samples : {1u, 2u, 4u}) {
    CoinRenderOptions options; options.sampleCount = samples;
    std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createWindow(
        native, SbVec2i32(size, size), options));
    CoinRenderAction action(SbViewportRegion(size, size));
    const unsigned char green = samples == 1 ? 0 : 64;
    const unsigned char blue = samples == 4 ? 64 : (samples == 1 ? 64 : 0);
    action.setBackgroundColor(SbColor4f(0, green / 255.0f, blue / 255.0f, 1));
    action.setRenderTarget(target.get());
    action.apply(scene);
    char adapter[256] = {0};
    coin_wgpu_get_adapter_info(adapter, sizeof(adapter));
    if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
      std::fprintf(stderr, "window msaa %ux failed: %s\n", samples,
                   action.getLastError().getString());
      passed = false;
    } else {
      std::vector<Pixel> pixels;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      do {
        pixels = capture(display, window, size);
        if (!pixels.empty() && pixels[0].g == green && pixels[0].b == blue) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } while (std::chrono::steady_clock::now() < deadline);
      size_t partial = 0;
      for (const Pixel& pixel : pixels) if (pixel.r > 0 && pixel.r < 255) ++partial;
      std::printf("msaa_window adapter=%s samples=%u partial_red=%zu stamp=%u,%u serial=%llu\n",
                  adapter,
                  samples, partial, pixels.empty() ? 0 : pixels[0].g,
                  pixels.empty() ? 0 : pixels[0].b,
                  static_cast<unsigned long long>(target->getLastSubmissionSerial()));
      passed &= pixels.size() == size_t(size) * size &&
                pixels[0].g == green && pixels[0].b == blue &&
                (samples == 1 ? partial == 0 : partial > 0) &&
                target->getLastSubmissionSerial() > 0;
      if (passed && samples == 4) {
        constexpr int larger = 48;
        XResizeWindow(display, window, larger, larger); XSync(display, False);
        passed &= target->resize(SbVec2i32(larger, larger));
        action.setViewportRegion(SbViewportRegion(larger, larger));
        action.setBackgroundColor(SbColor4f(0, 128.0f / 255.0f, 32.0f / 255.0f, 1));
        action.apply(scene);
        const auto resizeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
          pixels = capture(display, window, larger);
          if (!pixels.empty() && pixels[0].g == 128 && pixels[0].b == 32) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < resizeDeadline);
        passed &= action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  pixels.size() == size_t(larger) * larger &&
                  pixels[0].g == 128 && pixels[0].b == 32;
        const uint64_t resizedSerial = target->getLastSubmissionSerial();
        coin_wgpu_inject_fault(102); // one OUTDATED acquire, then reconfigure/retry
        action.setBackgroundColor(SbColor4f(0, 32.0f / 255.0f, 128.0f / 255.0f, 1));
        action.apply(scene);
        coin_wgpu_inject_fault(0);
        const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
          pixels = capture(display, window, larger);
          if (!pixels.empty() && pixels[0].g == 32 && pixels[0].b == 128) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < retryDeadline);
        const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                               target->getLastSubmissionSerial() > resizedSerial &&
                               pixels.size() == size_t(larger) * larger &&
                               pixels[0].g == 32 && pixels[0].b == 128;
        passed &= recovered;
        std::printf("msaa_window resize_48_recovery=%d\n", int(recovered));
      }
    }
    action.setRenderTarget(nullptr);
    if (!passed) break;
  }
  scene->unref();
  XDestroyWindow(display, window); XCloseDisplay(display);
  return passed ? 0 : 1;
}
