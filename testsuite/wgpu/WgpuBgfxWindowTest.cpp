#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuNativeSurface.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
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
#include <thread>
#include <vector>

namespace {
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

bool drawFrames(SoWgpuRenderAction & action, SoSeparator * scene, int count)
{
  for (int i = 0; i < count; ++i) {
    action.apply(scene);
    if (action.getLastStatus() != SoWgpuRenderAction::SUCCESS) {
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
  SoWgpuRenderAction::initClass();
  CoinWgpuExperimentalCapabilities caps{};
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_XLIB_WINDOW, &caps, sizeof(caps)) != 0 ||
      caps.backend != COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION) {
    XCloseDisplay(display);
    std::cerr << "BGFX Xlib window capability is not advertised\n";
    return 1;
  }

  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      20, 20, 96, 96, 0, 0, 0);
  XMapWindow(display, window);
  XSync(display, False);

  SoWgpuNativeSurfaceDescriptor desc{};
  desc.abiVersion = COIN_WGPU_NATIVE_SURFACE_ABI_VERSION;
  desc.structSize = sizeof(desc);
  desc.type = COIN_WGPU_SURFACE_XLIB;
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

  int result = 0;
  {
    std::unique_ptr<SoWgpuRenderTarget> target(
      SoWgpuRenderTarget::createWindow(desc, SbVec2i32(96, 96)));
    if (target->getStatus() != SoWgpuRenderTarget::TARGET_READY) {
      std::cerr << "Cannot create BGFX Xlib target: "
                << target->getLastError() << '\n';
      result = 1;
    } else {
      SoWgpuRenderAction action;
      action.setRenderTarget(target.get());
      action.setViewportRegion(SbViewportRegion(96, 96));
      action.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.1f, 1.0f));

      action.apply(scene);
      if (action.getLastStatus() == SoWgpuRenderAction::NOT_READY) {
        std::cout << "BGFX Xlib renderer unavailable: "
                  << action.getLastError().getString() << '\n';
        result = 77;
      } else if (action.getLastStatus() != SoWgpuRenderAction::SUCCESS ||
                 !drawFrames(action, scene, 8) ||
                 !centerIsRed(display, window, 96, 96)) {
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
        XResizeWindow(display, window, 128, 96);
        XSync(display, False);
        target->resize(SbVec2i32(128, 96));
        action.setViewportRegion(SbViewportRegion(128, 96));
        if (!drawFrames(action, scene, 8) ||
            !centerIsRed(display, window, 128, 96)) {
          std::cerr << "BGFX Xlib resize failed\n";
          result = 1;
        }
      }
      if (result == 0) {
        target->resize(SbVec2i32(0, 0));
        action.apply(scene);
        if (action.getLastStatus() != SoWgpuRenderAction::NOT_READY) {
          std::cerr << "BGFX zero-size window was not suspended\n";
          result = 1;
        }
      }
      if (result == 0) {
        target->resize(SbVec2i32(128, 96));
        action.setViewportRegion(SbViewportRegion(128, 96));
        if (!drawFrames(action, scene, 8) ||
            !centerIsRed(display, window, 128, 96)) {
          std::cerr << "BGFX Xlib window restoration failed\n";
          result = 1;
        }
      }
    }
  }
  scene->unref();
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  if (result == 0) std::cout << "BGFX Xlib window presentation passed\n";
  return result;
}
