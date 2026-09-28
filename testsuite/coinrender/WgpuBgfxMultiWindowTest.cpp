#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Scene {
  SoSeparator * root;
  SoOrthographicCamera * camera;
  SoMaterial * material;
  explicit Scene(int color) : root(new SoSeparator), camera(new SoOrthographicCamera) {
    root->ref();
    camera->position.setValue(0, 0, 4);
    camera->nearDistance = 0.1f;
    camera->farDistance = 10.0f;
    camera->height = 3.0f;
    root->addChild(camera);
    auto * model = new SoLightModel;
    model->model = SoLightModel::BASE_COLOR;
    root->addChild(model);
    auto * transparency = new SoTransparencyType;
    transparency->value = SoTransparencyType::SORTED_OBJECT_SORTED_TRIANGLE_BLEND;
    root->addChild(transparency);
    material = new SoMaterial;
    material->diffuseColor.setValue(color == 0 ? 0.9f : 0,
                                   color == 1 ? 0.9f : 0,
                                   color == 2 ? 0.9f : 0);
    root->addChild(material);
    root->addChild(new SoCube);
  }
  ~Scene() { root->unref(); }
};

unsigned int channel(unsigned long pixel, unsigned long mask) {
  if (!mask) return 0;
  unsigned long value = pixel & mask;
  while (!(mask & 1UL)) { mask >>= 1; value >>= 1; }
  return static_cast<unsigned int>(value * 255UL / mask);
}

bool windowColor(Display * display, Window window, int width, int height, int color, unsigned int minimum = 150) {
  XSync(display, False);
  XImage * image = XGetImage(display, window, width / 2, height / 2,
                            1, 1, AllPlanes, ZPixmap);
  if (!image) return false;
  const unsigned long pixel = XGetPixel(image, 0, 0);
  const unsigned int rgb[] = {channel(pixel, image->red_mask),
                             channel(pixel, image->green_mask),
                             channel(pixel, image->blue_mask)};
  XDestroyImage(image);
  std::cout << "window=" << window << " center=" << rgb[0] << ','
            << rgb[1] << ',' << rgb[2] << " expected=" << color << '\n';
  if (color < 0) return rgb[0] < 30 && rgb[1] < 30 && rgb[2] < 30;
  return rgb[color] > minimum && rgb[(color + 1) % 3] < 30 && rgb[(color + 2) % 3] < 30;
}

bool draw(CoinRenderAction & action, Scene & scene, const char * stage) {
  action.apply(scene.root);
  if (action.getLastStatus() == CoinRenderAction::SUCCESS) return true;
  std::cerr << stage << ": " << action.getLastError().getString() << '\n';
  return false;
}

std::unique_ptr<CoinRenderTarget> makeWindow(Display * display, Window window, int width, int height) {
  CoinRenderNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_RENDER_SURFACE_XLIB;
  desc.native.xlib.display = display;
  desc.native.xlib.window = window;
  return std::unique_ptr<CoinRenderTarget>(
    CoinRenderTarget::createWindow(desc, SbVec2i32(width, height)));
}
}

#define CHECK(condition, message) do { if (!(condition)) { \
  std::cerr << "FAIL: " << message << '\n'; return 1; } } while (false)

int main() {
  Display * display = XOpenDisplay(nullptr);
  if (!display) { std::cout << "X11 display unavailable\n"; return 77; }
  SoDB::init();
  CoinRenderAction::initClass();
  Scene red(0), green(1), blue(2);
  const char * renderer = std::getenv("COIN_BGFX_RENDERER");
  // GL window readback is not guaranteed by XGetImage. Its render status and
  // the interleaved GPU offscreen pixels are still mandatory, never skipped.
  const bool readWindow = !renderer || std::string(renderer) != "opengl";
  std::unique_ptr<CoinRenderTarget> offscreen;
  CoinRenderAction offscreenAction(SbViewportRegion(48, 48));
  auto createOffscreen = [&]() {
    offscreen.reset(CoinRenderTarget::createOffscreen(SbVec2i32(48, 48)));
    offscreen->setDepthReadbackEnabled(FALSE);
    offscreenAction.setRenderTarget(offscreen.get());
    offscreenAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    return offscreen && offscreen->getStatus() == CoinRenderTarget::TARGET_READY &&
      draw(offscreenAction, blue, "offscreen creation");
  };
  if (std::getenv("COIN_BGFX_TEST_OFFSCREEN_FIRST"))
    CHECK(createOffscreen(), "offscreen-first initialization");
  Window first = XCreateSimpleWindow(display, DefaultRootWindow(display), 20, 20, 96, 96, 0, 0, 0);
  Window second = XCreateSimpleWindow(display, DefaultRootWindow(display), 180, 20, 80, 112, 0, 0, 0);
  XMapWindow(display, first);
  XMapWindow(display, second);
  XSync(display, False);
  auto firstTarget = makeWindow(display, first, 96, 96);
  auto secondTarget = makeWindow(display, second, 80, 112);
  CHECK(firstTarget && firstTarget->getStatus() == CoinRenderTarget::TARGET_READY, "first target creation");
  CHECK(secondTarget && secondTarget->getStatus() == CoinRenderTarget::TARGET_READY, "simultaneous second target creation");
  CoinRenderAction firstAction(SbViewportRegion(96, 96));
  CoinRenderAction secondAction(SbViewportRegion(80, 112));
  firstAction.setRenderTarget(firstTarget.get());
  secondAction.setRenderTarget(secondTarget.get());
  firstAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  secondAction.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  if (!offscreen) CHECK(createOffscreen(), "offscreen alongside native windows");
  auto frames = [&]() {
    for (int i = 0; i < 8; ++i) {
      if (firstTarget && !draw(firstAction, red, "first native frame")) return false;
      if (!draw(secondAction, green, "second native frame") ||
          !draw(offscreenAction, blue, "interleaved offscreen frame")) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    std::vector<uint8_t> rgba;
    offscreen->readbackRGBA(rgba);
    const size_t center = (24u * 48u + 24u) * 4u;
    return rgba.size() == 48u * 48u * 4u && rgba[center] < 30 &&
      rgba[center + 1] < 30 && rgba[center + 2] > 150;
  };
  CHECK(frames(), "independent interleaved frames/readback");
  CHECK(!readWindow || (windowColor(display, first, 96, 96, 0) &&
                       windowColor(display, second, 80, 112, 1)), "independent native colors");
  red.camera->position.setValue(8, 0, 4);
  CHECK(frames(), "independent camera update");
  CHECK(!readWindow || (windowColor(display, first, 96, 96, -1) &&
                       windowColor(display, second, 80, 112, 1)), "camera update leaked between targets");
  red.camera->position.setValue(0, 0, 4);
  // Composition changes must retain the native surface, not create a second
  // EGL surface for an X window before its old surface has been retired.
  red.material->transparency.setValue(0.5f);
  CHECK(frames(), "opaque-to-transparent native composition switch");
  CHECK(!readWindow || (windowColor(display, first, 96, 96, 0, 50) &&
                       windowColor(display, second, 80, 112, 1)), "transparent composition switch leaked between targets");
  red.material->transparency.setValue(0.0f);
  CHECK(frames(), "transparent-to-opaque native composition switch");
  red.material->transparency.setValue(0.5f);
  CHECK(frames(), "second opaque-to-transparent native composition switch");
  XResizeWindow(display, first, 128, 72);
  XSync(display, False);
  firstTarget->resize(SbVec2i32(128, 72));
  firstAction.setViewportRegion(SbViewportRegion(128, 72));
  CHECK(secondTarget->getSize() == SbVec2i32(80, 112), "resize changed second target size");
  CHECK(frames(), "isolated resize");
  CHECK(!readWindow || (windowColor(display, first, 128, 72, 0, 50) &&
                       windowColor(display, second, 80, 112, 1)), "isolated resize pixels");
  red.material->transparency.setValue(0.0f);
  CHECK(frames(), "opaque restoration after transparent resize");
  firstTarget->resize(SbVec2i32(0, 0));
  firstAction.apply(red.root);
  CHECK(firstAction.getLastStatus() == CoinRenderAction::NOT_READY, "zero-size target suspension");
  CHECK(draw(secondAction, green, "second while first suspended"), "suspension leaked between targets");
  firstAction.setRenderTarget(nullptr);
  firstTarget.reset();
  XDestroyWindow(display, first);
  CHECK(frames(), "surviving second target after destroying initializer");
  CHECK(!readWindow || windowColor(display, second, 80, 112, 1), "second window lost presentation after owner destruction");
  first = XCreateSimpleWindow(display, DefaultRootWindow(display), 20, 160, 128, 72, 0, 0, 0);
  XMapWindow(display, first);
  XSync(display, False);
  firstTarget = makeWindow(display, first, 128, 72);
  CHECK(firstTarget && firstTarget->getStatus() == CoinRenderTarget::TARGET_READY, "replacement native target creation");
  firstAction.setRenderTarget(firstTarget.get());
  CHECK(frames(), "replacement target with surviving second target");
  CHECK(!readWindow || (windowColor(display, first, 128, 72, 0) &&
                       windowColor(display, second, 80, 112, 1)), "replacement target independent pixels");
  firstAction.setRenderTarget(nullptr);
  secondAction.setRenderTarget(nullptr);
  offscreenAction.setRenderTarget(nullptr);
  firstTarget.reset();
  secondTarget.reset();
  offscreen.reset();
  XDestroyWindow(display, first);
  XDestroyWindow(display, second);
  // As in the single-window test, do not close the driver's Xlib display.
  std::cout << "BGFX simultaneous native targets and offscreen lifecycle passed\n";
  return 0;
}
