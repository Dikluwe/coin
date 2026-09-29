#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#if defined(Status)
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
class GLTraversalSpy : public SoGLRenderAction {
public:
  GLTraversalSpy() : SoGLRenderAction(SbViewportRegion(96, 96)), count(0) {}
  int count;
protected:
  void beginTraversal(SoNode *) override { ++count; }
};
unsigned int channel(unsigned long pixel, unsigned long mask)
{
  if (mask == 0) return 0;
  unsigned long value = pixel & mask;
  while ((mask & 1UL) == 0) {
    mask >>= 1;
    value >>= 1;
  }
  return static_cast<unsigned int>((value * 255UL) / mask);
}

bool centerIsRed(Display * display, Window window, int width, int height)
{
  XSync(display, False);
  XImage * image = XGetImage(display, window, width / 2, height / 2,
                             1, 1, AllPlanes, ZPixmap);
  if (!image) return false;
  const unsigned long pixel = XGetPixel(image, 0, 0);
  const unsigned int red = channel(pixel, image->red_mask);
  const unsigned int green = channel(pixel, image->green_mask);
  const unsigned int blue = channel(pixel, image->blue_mask);
  XDestroyImage(image);
  return red > 120 && red > green * 2 && red > blue * 2;
}

bool drawAdapterFrames(CoinRenderManagerAdapter & adapter, int count)
{
  for (int i = 0; i < count; ++i) {
    if (adapter.render() != CoinRenderAction::SUCCESS) {
      std::cerr << "BGFX SoRenderManager adapter frame failed: "
                << adapter.getLastError().getString() << "\n";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return true;
}

bool drawFrames(CoinRenderAction & action, SoSeparator * scene, int count)
{
  for (int i = 0; i < count; ++i) {
    action.apply(scene);
    if (action.getLastStatus() != CoinRenderAction::SUCCESS) {
      std::cerr << "BGFX window frame failed: "
                << action.getLastError().getString() << '\n';
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return true;
}
}

int main()
{
  if (!std::getenv("DISPLAY")) {
    std::cout << "X11 display unavailable\n";
    return 77;
  }
  Display * display = XOpenDisplay(nullptr);
  if (!display) {
    std::cout << "Cannot open X11 display\n";
    return 77;
  }

  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(
        COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps)) != 0 ||
      caps.backend != COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION) {
    // BGFX may retain driver-side Xlib state until process exit.
    std::cerr << "BGFX Xlib window capability is not advertised\n";
    return 1;
  }

  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      20, 20, 96, 96, 0, 0, 0);
  XMapWindow(display, window);
  XSync(display, False);

  CoinRenderNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_RENDER_SURFACE_XLIB;
  desc.native.xlib.display = display;
  desc.native.xlib.window = window;

  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  camera->height = 2.0f;
  scene->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  scene->addChild(model);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.9f, 0.0f, 0.0f);
  scene->addChild(material);
  scene->addChild(new SoCube);

  const bool adapterMode = std::getenv("COIN_BGFX_TEST_RENDER_MANAGER_ADAPTER") != nullptr;
  const char * renderer = std::getenv("COIN_BGFX_RENDERER");
  const bool canReadWindow = renderer == nullptr || std::string(renderer) != "opengl";
  int result = 0;
  if (!adapterMode) {
    std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createWindow(desc, SbVec2i32(96, 96)));
    if (target->getStatus() != CoinRenderTarget::TARGET_READY) {
      std::cerr << "Cannot create BGFX Xlib target: "
                << target->getLastError() << '\n';
      result = 1;
    } else {
      CoinRenderAction action;
      action.setRenderTarget(target.get());
      action.setViewportRegion(SbViewportRegion(96, 96));
      action.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.1f, 1.0f));

      action.apply(scene);
      if (action.getLastStatus() == CoinRenderAction::NOT_READY) {
        std::cout << "BGFX Xlib renderer unavailable: "
                  << action.getLastError().getString() << '\n';
        result = 77;
      } else if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
                 !drawFrames(action, scene, 8) ||
                 canReadWindow && !centerIsRed(display, window, 96, 96)) {
        std::cerr << "BGFX Xlib window did not present the red cube\n";
        result = 1;
      }

      std::vector<uint8_t> rgba;
      target->readbackRGBA(rgba);
      size_t borrowedBytes = 1;
      if (result == 0 &&
          (!rgba.empty() || target->borrowRGBA(borrowedBytes) != nullptr ||
           borrowedBytes != 0)) {
        std::cerr << "Window presentation exposed an offscreen readback\n";
        result = 1;
      }

      if (result == 0) {
        if (!target->requestWindowReadbackRGBA()) {
          std::cerr << "Window RGBA request rejected: " << target->getLastError() << '\n';
          result = 1;
        } else {
          action.apply(scene);
          target->readbackRGBA(rgba);
          const size_t center = (48u * 96u + 48u) * 4u;
          if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
              rgba.size() != 96u * 96u * 4u ||
              rgba[center] < 120 || rgba[center] <= rgba[center + 1] * 2) {
            std::cerr << "Explicit window RGBA capture missed the red cube\n";
            result = 1;
          }
          if (result == 0) {
            action.apply(scene);
            target->readbackRGBA(rgba);
            if (action.getLastStatus() != CoinRenderAction::SUCCESS || !rgba.empty()) {
              std::cerr << "Ordinary window render exposed a stale capture\n";
              result = 1;
            }
          }
        }
      }

      if (result == 0) {
        XResizeWindow(display, window, 128, 96);
        XSync(display, False);
        target->resize(SbVec2i32(128, 96));
        action.setViewportRegion(SbViewportRegion(128, 96));
        if (!drawFrames(action, scene, 8) ||
            canReadWindow && !centerIsRed(display, window, 128, 96)) {
          std::cerr << "BGFX Xlib resize failed\n";
          result = 1;
        }
      }
      if (result == 0) {
        target->resize(SbVec2i32(0, 0));
        action.apply(scene);
        if (action.getLastStatus() != CoinRenderAction::NOT_READY) {
          std::cerr << "BGFX zero-size window was not suspended\n";
          result = 1;
        }
      }
      if (result == 0) {
        target->resize(SbVec2i32(128, 96));
        action.setViewportRegion(SbViewportRegion(128, 96));
        if (!drawFrames(action, scene, 8) ||
            canReadWindow && !centerIsRed(display, window, 128, 96)) {
          std::cerr << "BGFX Xlib window restoration failed\n";
          result = 1;
        }
      }
    }
  }
  if (adapterMode && result == 0) {
    SoRenderManager source;
    GLTraversalSpy glspy;
    source.setGLRenderAction(&glspy);
    source.setSceneGraph(scene);
    source.setViewportRegion(SbViewportRegion(96, 96));
    source.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.1f, 1.0f));
    CoinRenderManagerAdapter adapter(source, desc, SbVec2i32(96, 96));
    if (!drawAdapterFrames(adapter, 4)) {
      std::cerr << "SoRenderManager adapter did not present an exposure\n";
      result = 1;
    } else {
      XResizeWindow(display, window, 128, 96);
      XSync(display, False);
      source.setViewportRegion(SbViewportRegion(128, 96));
      if (!adapter.resize(SbVec2i32(128, 96)) ||
          source.getViewportRegion().getViewportSizePixels() != SbVec2s(128, 96) ||
          !drawAdapterFrames(adapter, 4)) {
        std::cerr << "SoRenderManager adapter resize lifecycle failed\n";
        result = 1;
      }
    }
    if (glspy.count != 0) {
      std::cerr << "Adapter invoked auxiliary GL rendering\n";
      result = 1;
    }
    std::cout << "Adapter GL traversals=" << glspy.count << '\n';
    source.setSceneGraph(nullptr);
  }
  scene->unref();
  XDestroyWindow(display, window);
  // BGFX may retain driver-side Xlib state until process exit.
  if (result == 0) std::cout << "BGFX Xlib window presentation passed\n";
  return result;
}
