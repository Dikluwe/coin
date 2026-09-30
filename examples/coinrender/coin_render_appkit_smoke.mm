/* AppKit/Metal smoke. AppKit owns the windows and CAMetalLayers; CoinRender
 * borrows the layer pointers and publishes frames through the common target. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void pump() {
  NSEvent * event;
  while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                    untilDate:[NSDate distantPast]
                                       inMode:NSDefaultRunLoopMode dequeue:YES])) {
    [NSApp sendEvent:event];
  }
  [NSApp updateWindows];
}

SbVec2i32 pixels(NSView * view) {
  const NSRect rect = [view convertRectToBacking:[view bounds]];
  return SbVec2i32(int(std::lround(rect.size.width)),
                   int(std::lround(rect.size.height)));
}

uint64_t checksum(const std::vector<uint8_t> & bytes) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (uint8_t byte : bytes) {
    value ^= byte;
    value *= UINT64_C(1099511628211);
  }
  return value;
}

struct WindowRun {
  __strong NSWindow * window = nil;
  __strong NSView * view = nil;
  __strong CAMetalLayer * layer = nil;
  std::unique_ptr<CoinRenderTarget> target;
  std::unique_ptr<CoinRenderAction> action;
};

bool create(WindowRun & run, int index) {
  run.window = [[NSWindow alloc]
    initWithContentRect:NSMakeRect(100 + index * 400, 100, 360, 280)
              styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                         NSWindowStyleMaskResizable)
                backing:NSBackingStoreBuffered defer:NO];
  if (!run.window) return false;
  run.view = [run.window contentView];
  [run.view setWantsLayer:YES];
  run.layer = [CAMetalLayer layer];
  [run.view setLayer:run.layer];
  [run.window makeKeyAndOrderFront:nil];
  pump();
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_APPKIT_LAYER;
  native.native.appkit.metalLayer = (__bridge void *)run.layer;
  CoinRenderOptions options;
  options.renderer = COIN_RENDER_RENDERER_METAL;
  const SbVec2i32 size = pixels(run.view);
  run.target.reset(CoinRenderTarget::createWindow(native, size, options));
  if (!run.target || run.target->getStatus() != CoinRenderTarget::TARGET_READY) {
    std::cerr << "AppKit target creation failed: "
              << (run.target ? run.target->getLastError() : "null target") << '\n';
    return false;
  }
  run.action.reset(new CoinRenderAction);
  run.action->setRenderTarget(run.target.get());
  return true;
}

bool render(WindowRun & run, SoSeparator * scene, bool capture) {
  pump();
  const SbVec2i32 size = pixels(run.view);
  if (size[0] <= 0 || size[1] <= 0) return false;
  run.layer.contentsScale = run.window.backingScaleFactor;
  if (size != run.target->getSize() && !run.target->resize(size)) return false;
  run.action->setViewportRegion(SbViewportRegion(size[0], size[1]));
  if (capture && !run.target->requestWindowReadbackRGBA()) return false;
  run.action->apply(scene);
  if (run.action->getLastStatus() != CoinRenderAction::SUCCESS) {
    std::cerr << "AppKit render failed: " << run.action->getLastError().getString() << '\n';
    return false;
  }
  std::vector<uint8_t> rgba;
  run.target->readbackRGBA(rgba);
  if (!capture) return rgba.empty();
  if (rgba.size() != size_t(size[0]) * size_t(size[1]) * 4u) return false;
  const size_t center = (size_t(size[1] / 2) * size_t(size[0]) + size_t(size[0] / 2)) * 4u;
  if (rgba[center] < rgba[center + 1] + 50 || rgba[center] < rgba[center + 2] + 50) return false;
  std::cout << "appkit_rgba_fnv64=0x" << std::hex << checksum(rgba) << std::dec
            << " size=" << size[0] << 'x' << size[1]
            << " scale=" << run.window.backingScaleFactor << '\n';
  return true;
}
}

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    SoDB::init();
    CoinRenderAction::initClass();
    SoSeparator * scene = new SoSeparator;
    scene->ref();
    SoOrthographicCamera * camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 4);
    camera->height = 3;
    scene->addChild(camera);
    SoLightModel * model = new SoLightModel;
    model->model = SoLightModel::BASE_COLOR;
    scene->addChild(model);
    SoMaterial * material = new SoMaterial;
    material->diffuseColor.setValue(1, 0, 0);
    scene->addChild(material);
    scene->addChild(new SoCube);

    WindowRun windows[2];
    bool ok = create(windows[0], 0) && create(windows[1], 1);
    if (ok) {
      ok = render(windows[0], scene, true) && render(windows[1], scene, true) &&
           render(windows[0], scene, false) && render(windows[1], scene, false);
    }
    if (ok) {
      [windows[0].window setContentSize:NSMakeSize(520, 390)];
      ok = render(windows[0], scene, true) && render(windows[1], scene, true);
    }
    if (ok) {
      [windows[0].window miniaturize:nil];
      pump();
      ok = windows[0].target->resize(SbVec2i32(0, 0)) &&
           windows[0].target->getStatus() == CoinRenderTarget::TARGET_NOT_READY;
      if (ok) {
        windows[0].action->apply(scene);
        ok = windows[0].action->getLastStatus() == CoinRenderAction::NOT_READY;
      }
      [windows[0].window deminiaturize:nil];
      ok = ok && render(windows[0], scene, true);
    }
    CoinRenderCapabilities caps{};
    if (ok) {
      const int32_t query = coin_render_query_capabilities_for_renderer(
        COIN_RENDER_EXPERIMENTAL_APPKIT_LAYER, COIN_RENDER_RENDERER_METAL,
        &caps, sizeof(caps));
      ok = query == 0 && caps.gpu_available && caps.renderer == COIN_RENDER_RENDERER_METAL;
      if (ok) std::cout << "adapter=\"" << caps.adapter_name << "\" renderer=" << caps.renderer << '\n';
    }
    for (auto & window : windows) {
      window.action.reset();
      window.target.reset();
      if (window.window) [window.window close];
      window.layer = nil;
      window.view = nil;
      window.window = nil;
    }
    scene->unref();
    if (!ok) std::cerr << "P22 AppKit smoke failed\n";
    return ok ? 0 : 1;
  }
}
