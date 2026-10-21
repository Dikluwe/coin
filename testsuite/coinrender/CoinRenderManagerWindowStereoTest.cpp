#include "rendering/coinrender/CoinRenderDiagnosticShell.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoCube.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {
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
  if (!diagnostic.empty()) return 2;
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
  SoSeparator * base = new SoSeparator; base->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 6);
  camera->nearDistance = 1; camera->farDistance = 20; camera->focalDistance = 6;
  base->addChild(camera);
  SoLightModel * lighting = new SoLightModel;
  lighting->model = SoLightModel::BASE_COLOR;
  base->addChild(lighting);
  SoMaterial * white = new SoMaterial;
  white->diffuseColor.setValue(1, 1, 1);
  base->addChild(white);
  SoCube * cube = new SoCube;
  cube->width = 1.2f; cube->height = 1.2f; cube->depth = 1.2f;
  base->addChild(cube);
  SoSeparator * overlay = new SoSeparator; overlay->ref();
  SoOrthographicCamera * overlayCamera = new SoOrthographicCamera;
  overlayCamera->position.setValue(0, 0, 4);
  overlayCamera->height = 4;
  overlay->addChild(overlayCamera);
  SoLightModel * overlayLighting = new SoLightModel;
  overlayLighting->model = SoLightModel::BASE_COLOR;
  overlay->addChild(overlayLighting);
  SoMaterial * green = new SoMaterial;
  green->diffuseColor.setValue(0, 1, 0);
  overlay->addChild(green);
  SoCube * overlayCube = new SoCube;
  overlayCube->width = 0.8f; overlayCube->height = 0.8f; overlayCube->depth = 0.2f;
  overlay->addChild(overlayCube);
  SoRenderManager manager;
  manager.setSceneGraph(base);
  manager.setCamera(camera);
  manager.setStereoMode(SoRenderManager::ANAGLYPH);
  manager.setStereoOffset(2.0f);
  manager.setViewportRegion(SbViewportRegion(64, 64));
  std::unique_ptr<CoinRenderManagerAdapter> offscreen(
    new CoinRenderManagerAdapter(manager, SbVec2i32(64, 64), options));
  std::unique_ptr<CoinRenderManagerAdapter> presented(
    new CoinRenderManagerAdapter(manager, surface, SbVec2i32(64, 64), options));
  std::vector<uint8_t> first, opaqueLayer, screenDoorFrame, fullTextureFrame;
  SoTexture2 * texture = nullptr;
  size_t opaqueRedOnly = 0, screenDoorRedOnly = 0;
  size_t fullViewportRedOnly = 0, fullViewportGreen = 0;
  bool ok = true;
  for (int frame = 0; frame < 9 && ok; ++frame) {
    const int size = frame == 1 || frame >= 6 ? 96 : 64;
    if (frame == 1) manager.addSuperimposition(overlay,
      SoRenderManager::Superimposition::AUTOREDRAW |
      SoRenderManager::Superimposition::ZBUFFERON |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    if (frame == 3) {
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SCREEN_DOOR);
      white->transparency = 0.5f;
    }
    if (frame == 4)
      manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
    if (frame == 5) {
      white->transparency = 0.0f;
      texture = new SoTexture2;
      const uint8_t whiteAlpha[] = {255, 255, 255, 128};
      texture->image.setValue(SbVec2s(1, 1), 4, whiteAlpha);
      texture->model = SoTexture2::MODULATE;
      base->insertChild(texture, 3);
    }
    if (frame == 6) {
      const uint8_t variedAlpha[] = {
        255, 255, 255, 128, 255, 255, 255, 64,
        255, 255, 255, 192, 255, 255, 255, 96
      };
      texture->image.setValue(SbVec2s(2, 2), 4, variedAlpha);
    }
    if (frame == 8) white->transparency = 0.5f;
    if (frame == 1 || frame == 2 || frame == 6) {
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
    SbViewportRegion viewport(size, size);
    if (frame == 7) viewport.setViewportPixels(8, 8, 80, 80);
    manager.setViewportRegion(viewport);
    manager.setBackgroundColor(SbColor4f((17 + (frame == 8 ? 6 : frame) * 19) / 255.0f,
                                         33 / 255.0f, 49 / 255.0f, 1.0f));
    if (!ok || offscreen->render() != CoinRenderAction::SUCCESS ||
        presented->render() != CoinRenderAction::SUCCESS) {
      std::fprintf(stderr, "stereo window render failed: %s\n",
                   presented->getLastError().getString());
      ok = false; break;
    }
    std::vector<uint8_t> expected;
    offscreen->getSceneManager()->getRenderTarget()->readbackRGBA(expected);
    if (expected.size() != size_t(size) * size * 4) { ok = false; break; }
    if (frame == 0) first = expected;
    if (frame == 2) opaqueLayer = expected;
    if (frame == 3) screenDoorFrame = expected;
    if (frame == 6) fullTextureFrame = expected;
    std::vector<unsigned char> observed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      observed = capture(display, window, size, size);
      if (observed.size() == size_t(size) * size * 3 &&
          std::equal(observed.begin(), observed.begin() + 3, expected.begin())) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    size_t mismatch = 0, redOnly = 0, green = 0;
    std::array<bool, 256> redLevels{};
    unsigned maxGreenWithLowRed = 0;
    if (observed.size() == size_t(size) * size * 3)
      for (size_t pixel = 0; pixel < size_t(size) * size; ++pixel) {
        const size_t rgb = pixel * 3, rgba = pixel * 4;
        mismatch += observed[rgb] != expected[rgba] ||
                    observed[rgb + 1] != expected[rgba + 1] ||
                    observed[rgb + 2] != expected[rgba + 2];
        redOnly += expected[rgba] > 150 && expected[rgba + 1] < 40;
        if (expected[rgba] > 40 && expected[rgba + 1] < 40)
          redLevels[expected[rgba]] = true;
        green += expected[rgba + 1] > 150 && expected[rgba] < 40;
        if (expected[rgba] < 40)
          maxGreenWithLowRed = std::max(maxGreenWithLowRed, unsigned(expected[rgba + 1]));
      }
    if (frame == 2) opaqueRedOnly = redOnly;
    if (frame == 3) screenDoorRedOnly = redOnly;
    if (frame == 6) { fullViewportRedOnly = redOnly; fullViewportGreen = green; }
    ok = observed.size() == size_t(size) * size * 3 && mismatch == 0 &&
         (frame == 8 || redOnly > (frame == 3 ? 10u : 20u)) &&
         (frame != 3 || redOnly < opaqueRedOnly) &&
         (frame != 4 || redOnly > screenDoorRedOnly) &&
         (frame == 0 || green > 20) &&
         camera->getStereoMode() == SoCamera::MONOSCOPIC;
    if (frame == 2) ok = ok && first != expected;
    if (frame == 3) ok = ok && opaqueLayer != expected;
    if (frame == 4) ok = ok && screenDoorFrame != expected;
    const size_t distinctRedLevels = std::count(redLevels.begin(), redLevels.end(), true);
    if (frame == 6) ok = ok && distinctRedLevels >= 3;
    if (frame == 7) ok = ok && distinctRedLevels >= 3 &&
      redOnly < fullViewportRedOnly && green < fullViewportGreen;
    size_t mixedChanged = 0;
    if (frame == 8 && fullTextureFrame.size() == expected.size())
      for (size_t i = 0; i < expected.size(); i += 4)
        mixedChanged += expected[i] != fullTextureFrame[i] ||
                        expected[i + 1] != fullTextureFrame[i + 1] ||
                        expected[i + 2] != fullTextureFrame[i + 2];
    if (frame == 8) ok = ok && distinctRedLevels >= 3 && mixedChanged > 100;
    std::printf("manager_window_anaglyph frame=%d size=%d mismatch=%zu red_only=%zu red_levels=%zu green=%zu mixed_changed=%zu max_green=%u center=%u,%u,%u result=%s\n",
      frame, size, mismatch, redOnly, distinctRedLevels, green, mixedChanged, maxGreenWithLowRed,
      expected[(size_t(size/2)*size+size/2)*4],
      expected[(size_t(size/2)*size+size/2)*4+1],
      expected[(size_t(size/2)*size+size/2)*4+2], ok ? "PASS" : "FAIL");
  }
  presented.reset(); offscreen.reset();
  manager.setCamera(nullptr);
  manager.setSceneGraph(nullptr);
  base->unref(); overlay->unref();
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return ok ? 0 : 1;
}
