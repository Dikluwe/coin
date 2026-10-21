#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
SoSeparator * quad(float extent, float z, const SbColor & color, float transparency)
{
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4);
  camera->height = 4;
  root->addChild(camera);
  SoLightModel * lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  root->addChild(lighting);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(color);
  material->transparency = transparency;
  root->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  const SbVec3f corners[] = {
    SbVec3f(-extent, -extent, z), SbVec3f(extent, -extent, z),
    SbVec3f(extent, extent, z), SbVec3f(-extent, extent, z)
  };
  coordinates->point.setValues(0, 4, corners);
  root->addChild(coordinates);
  SoFaceSet * face = new SoFaceSet;
  face->numVertices.set1Value(0, 4);
  root->addChild(face);
  return root;
}

unsigned char channel(unsigned long value, unsigned long mask)
{
  if (!mask) return 0;
  while (!(mask & 1)) { value >>= 1; mask >>= 1; }
  return static_cast<unsigned char>(((value & mask) * 255 + mask / 2) / mask);
}

std::vector<unsigned char> capture(Display * display, Window window, int width, int height)
{
  XImage * image = XGetImage(display, window, 0, 0, width, height, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<unsigned char> pixels(size_t(width) * height * 3);
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const unsigned long value = XGetPixel(image, x, y);
    const size_t slot = (size_t(y) * width + x) * 3;
    pixels[slot] = channel(value, image->red_mask);
    pixels[slot + 1] = channel(value, image->green_mask);
    pixels[slot + 2] = channel(value, image->blue_mask);
  }
  XDestroyImage(image);
  return pixels;
}
}

int main()
{
  SoDB::init();
  CoinRenderAction::initClass();
  std::string diagnostic;
  const CoinRenderOptions options = CoinRenderDiagnosticShell::renderOptions(diagnostic);
  if (!diagnostic.empty()) { std::fprintf(stderr, "%s\n", diagnostic.c_str()); return 2; }
  CoinRenderCapabilities capabilities{};
  if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
      options.renderer, &capabilities, sizeof(capabilities)) || !capabilities.gpu_available) return 77;
  Display * display = XOpenDisplay(nullptr);
  if (!display) return 77;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      32, 32, 64, 64, 0, 0, 0);
  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
  XMapRaised(display, window);
  XSync(display, False);
  CoinRenderNativeSurfaceDescriptor surface{};
  surface.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  surface.structSize = sizeof(surface);
  surface.type = COIN_RENDER_SURFACE_XLIB;
  surface.native.xlib.display = display;
  surface.native.xlib.window = window;
  SoSeparator * base = quad(0.85f, 0.0f, SbColor(1, 0, 0), 0.0f);
  SoRenderManager manager;
  manager.setSceneGraph(base);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  std::unique_ptr<CoinRenderManagerAdapter> offscreen(
    new CoinRenderManagerAdapter(manager, SbVec2i32(64, 64), options));
  std::unique_ptr<CoinRenderManagerAdapter> presented(
    new CoinRenderManagerAdapter(manager, surface, SbVec2i32(64, 64), options));
  std::printf("manager_window_adapter backend=%u renderer=%u vendor=0x%x adapter=%s\n",
    capabilities.backend, capabilities.renderer, capabilities.vendor_id, capabilities.adapter_name);
  bool ok = true;
  struct Case { const char * name; bool reverse; bool clearSecond; };
  const Case cases[] = {
    {"blend_door_depth", false, false}, {"blend_door_clear", false, true},
    {"door_blend_depth", true, false}, {"door_blend_clear", true, true}
  };
  std::vector<uint8_t> firstFrames[4];
  for (size_t scenario = 0; scenario < 4 && ok; ++scenario) {
    const Case & current = cases[scenario];
    SoSeparator * blue = quad(0.62f, 0.5f, SbColor(0, 0, 1), 0.5f);
    SoSeparator * green = quad(0.38f, current.reverse ? 0.75f : 0.25f,
                               SbColor(0, 1, 0), 0.5f);
    const int firstFlags = SoRenderManager::Superimposition::AUTOREDRAW |
                           SoRenderManager::Superimposition::ZBUFFERON;
    const int secondFlags = firstFlags |
      (current.clearSecond ? SoRenderManager::Superimposition::CLEARZBUFFER : 0);
    auto * first = manager.addSuperimposition(current.reverse ? green : blue, firstFlags);
    auto * second = manager.addSuperimposition(current.reverse ? blue : green, secondFlags);
    first->setTransparencyType(current.reverse ? SoGLRenderAction::SCREEN_DOOR :
                                              SoGLRenderAction::BLEND);
    second->setTransparencyType(current.reverse ? SoGLRenderAction::BLEND :
                                               SoGLRenderAction::SCREEN_DOOR);
  for (int frame = 0; frame < 3 && ok; ++frame) {
    const int size = frame == 1 ? 96 : 64;
    if (frame == 1 || frame == 2) {
      XResizeWindow(display, window, size, size);
      XSync(display, False);
      XWindowAttributes actual{};
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      do {
        XGetWindowAttributes(display, window, &actual);
        if (actual.width == size && actual.height == size) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      } while (std::chrono::steady_clock::now() < deadline);
      ok = actual.width == size && actual.height == size &&
        offscreen->resize(SbVec2i32(size, size)) &&
        presented->resize(SbVec2i32(size, size));
    }
    manager.setViewportRegion(SbViewportRegion(size, size));
    manager.setBackgroundColor(SbColor4f((17 + (scenario * 3 + frame) * 17) / 255.0f,
                                         33 / 255.0f, 49 / 255.0f, 1.0f));
    if (!ok || offscreen->render() != CoinRenderAction::SUCCESS ||
        presented->render() != CoinRenderAction::SUCCESS) {
      std::fprintf(stderr, "manager window render failed: %s\n",
        presented->getLastError().getString()); ok = false; break;
    }
    std::vector<uint8_t> expected;
    offscreen->getSceneManager()->getRenderTarget()->readbackRGBA(expected);
    if (expected.size() != size_t(size) * size * 4) { ok = false; break; }
    if (frame == 0) firstFrames[scenario] = expected;
    std::vector<unsigned char> observed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      observed = capture(display, window, size, size);
      if (observed.size() == size_t(size) * size * 3 &&
          std::equal(observed.begin(), observed.begin() + 3, expected.begin())) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    unsigned maximum = 0; size_t changed = 0, foreground = 0;
    size_t blendedBlue = 0, doorGreen = 0;
    if (observed.size() == size_t(size) * size * 3)
      for (size_t pixel = 0; pixel < size_t(size) * size; ++pixel) {
        bool different = false;
        for (size_t c = 0; c < 3; ++c) {
          const unsigned error = unsigned(std::abs(int(observed[pixel * 3 + c]) -
                                               int(expected[pixel * 4 + c])));
          maximum = std::max(maximum, error); different |= error > 1;
        }
        changed += different;
        foreground += std::abs(int(expected[pixel * 4]) - int(expected[0])) > 10 ||
                      std::abs(int(expected[pixel * 4 + 1]) - int(expected[1])) > 10 ||
                      std::abs(int(expected[pixel * 4 + 2]) - int(expected[2])) > 10;
        blendedBlue += expected[pixel * 4] > 40 && expected[pixel * 4 + 2] > 40 &&
                       expected[pixel * 4 + 2] > expected[pixel * 4 + 1] + 30;
        doorGreen += expected[pixel * 4 + 1] > expected[pixel * 4] + 40 &&
                     expected[pixel * 4 + 1] > expected[pixel * 4 + 2] + 40;
      }
    ok = observed.size() == size_t(size) * size * 3 && !changed &&
      foreground > 100 && blendedBlue > 0 &&
      (current.reverse == current.clearSecond ? doorGreen == 0 : doorGreen > 0);
    std::printf("manager_window_layers scenario=%s frame=%d size=%d max_rgb=%u mismatched_pixels=%zu foreground=%zu blended_blue=%zu door_green=%zu result=%s\n",
      current.name, frame, size, maximum, changed, foreground, blendedBlue, doorGreen,
      ok ? "PASS" : "FAIL");
  }
    manager.removeSuperimposition(second);
    manager.removeSuperimposition(first);
    green->unref(); blue->unref();
  }
  if (ok) {
    auto centerDifferent = [&](size_t a, size_t b) {
      size_t changedPixels = 0;
      for (int y = 22; y < 42; ++y) for (int x = 22; x < 42; ++x) {
        const size_t pixel = (size_t(y) * 64 + x) * 4;
        if (std::memcmp(firstFrames[a].data() + pixel, firstFrames[b].data() + pixel, 3))
          ++changedPixels;
      }
      return changedPixels;
    };
    const size_t forwardChanged = centerDifferent(0, 1);
    const size_t reverseChanged = centerDifferent(2, 3);
    std::printf("manager_window_depth_effect forward=%zu reverse=%zu result=%s\n",
      forwardChanged, reverseChanged,
      forwardChanged && reverseChanged ? "PASS" : "FAIL");
    ok = forwardChanged > 0 && reverseChanged > 0;
  }
  presented.reset(); offscreen.reset();
  manager.setSceneGraph(nullptr);
  base->unref();
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return ok ? 0 : 1;
}
