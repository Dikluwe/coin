#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

struct Scene { SoSeparator * root; SoCoordinate3 * points; SoMaterial * material; SoAlphaTest * alpha; };
struct Frame { std::vector<uint8_t> color; std::vector<float> depth; uint64_t serial = 0; };

static Scene quad(float extent, float z, const SbColor & color) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4); camera->height = 4;
  root->addChild(camera);
  auto * model = new SoLightModel; model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  auto * material = new SoMaterial; material->diffuseColor.setValue(color);
  root->addChild(material);
  auto * alpha = new SoAlphaTest; alpha->function = SoAlphaTest::NONE;
  root->addChild(alpha);
  auto * points = new SoCoordinate3;
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  points->point.setValues(0, 4, corners);
  root->addChild(points);
  auto * face = new SoFaceSet; face->numVertices.set1Value(0, 4);
  root->addChild(face);
  return {root, points, material, alpha};
}

static void setZ(Scene & scene, float extent, float z) {
  const SbVec3f corners[] = {
    SbVec3f(-extent,-extent,z), SbVec3f(extent,-extent,z),
    SbVec3f(extent,extent,z), SbVec3f(-extent,extent,z)
  };
  scene.points->point.setValues(0, 4, corners);
}

static uint8_t channel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { value >>= 1; mask >>= 1; }
  return uint8_t(((value & mask) * 255 + mask / 2) / mask);
}

static std::vector<uint8_t> captureWindow(Display * display, Window window, int size) {
  XSync(display, False);
  XImage * image = XGetImage(display, window, 0, 0, size, size, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<uint8_t> rgb(size_t(size) * size * 3u);
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const unsigned long value = XGetPixel(image, x, y);
    const size_t at = (size_t(y) * size + x) * 3u;
    rgb[at] = channel(value, image->red_mask);
    rgb[at+1] = channel(value, image->green_mask);
    rgb[at+2] = channel(value, image->blue_mask);
  }
  XDestroyImage(image);
  return rgb;
}

static bool sameRGB(const std::vector<uint8_t> & presented,
                    const std::vector<uint8_t> & rgba, int size,
                    unsigned & maximum, size_t & mismatched) {
  maximum = 0; mismatched = 0;
  if (presented.size() != size_t(size) * size * 3u ||
      rgba.size() != size_t(size) * size * 4u) return false;
  for (size_t pixel = 0; pixel < size_t(size) * size; ++pixel) {
    bool different = false;
    for (size_t c = 0; c < 3; ++c) {
      const unsigned delta = unsigned(std::abs(int(presented[pixel*3u+c]) -
                                                int(rgba[pixel*4u+c])));
      maximum = std::max(maximum, delta);
      different |= delta != 0;
    }
    mismatched += different;
  }
  return mismatched == 0;
}

static bool greenCenter(const Frame & frame, int size) {
  if (frame.color.size() != size_t(size) * size * 4u) return false;
  const size_t at = (size_t(size/2)*size+size/2)*4u;
  return frame.color[at+1u] > frame.color[at]*2u &&
         frame.color[at+1u] > frame.color[at+2u]*2u &&
         frame.color[at+1u] > 100;
}

static size_t changedContentInCenter(const Frame & baseline, unsigned baselineStamp,
                                     const Frame & candidate, unsigned candidateStamp,
                                     int size) {
  if (baseline.color.size() != candidate.color.size() ||
      baseline.color.size() != size_t(size) * size * 4u) return 0;
  size_t changed = 0;
  const int first = size/2 - size/8, last = size/2 + size/8;
  for (int y = first; y < last; ++y) for (int x = first; x < last; ++x) {
    const size_t at = (size_t(y)*size+x)*4u;
    const bool baselineIsClear =
        std::abs(int(baseline.color[at])-int(baselineStamp)) <= 1 &&
        std::abs(int(baseline.color[at+1])-17) <= 1 &&
        std::abs(int(baseline.color[at+2])-29) <= 1;
    bool pixelChanged = false;
    for (size_t channel = 0; channel < 3; ++channel) {
      const int expected = baselineIsClear ?
          int(channel == 0 ? candidateStamp : (channel == 1 ? 17 : 29)) :
          int(baseline.color[at+channel]);
      pixelChanged |= std::abs(int(candidate.color[at+channel])-expected) > 2;
    }
    changed += pixelChanged;
  }
  return changed;
}

static bool waitMatch(Display * display, Window window, int size,
                      const Frame & reference, std::vector<uint8_t> & presented) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  unsigned maximum = 255; size_t changed = size_t(size) * size;
  do {
    presented = captureWindow(display, window, size);
    if (sameRGB(presented, reference.color, size, maximum, changed)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  std::fprintf(stderr, "draw_modes_window image mismatch size=%d max_rgb=%u pixels=%zu\n",
               size, maximum, changed);
  return false;
}

static bool resize(Display * display, Window window,
                   CoinRenderManagerAdapter & offscreen,
                   CoinRenderManagerAdapter & presented,
                   SoRenderManager & manager, int size) {
  XResizeWindow(display, window, size, size); XSync(display, False);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  XWindowAttributes attributes{};
  do {
    XGetWindowAttributes(display, window, &attributes);
    if (attributes.width == size && attributes.height == size) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  manager.setViewportRegion(SbViewportRegion(size, size));
  return attributes.width == size && attributes.height == size &&
         offscreen.resize(SbVec2i32(size, size)) &&
         presented.resize(SbVec2i32(size, size));
}

static bool renderPair(SoRenderManager & manager,
                       CoinRenderManagerAdapter & offscreen,
                       CoinRenderManagerAdapter & presented,
                       Display * display, Window window, int size,
                       unsigned stamp, Frame & result,
                       std::vector<uint8_t> & visible) {
  manager.setBackgroundColor(SbColor4f(float(stamp)/255.0f, 17.0f/255.0f,
                                       29.0f/255.0f, 1));
  const auto offscreenStatus = offscreen.render();
  const auto windowStatus = presented.render();
  CoinRenderTarget * target = offscreen.getSceneManager()->getRenderTarget();
  target->readbackRGBA(result.color);
  target->readbackDepth(result.depth);
  result.serial = target->getLastSubmissionSerial();
  const bool matched = waitMatch(display, window, size, result, visible);
  const bool good = offscreenStatus == CoinRenderAction::SUCCESS &&
      windowStatus == CoinRenderAction::SUCCESS &&
      result.color.size() == size_t(size) * size * 4u &&
      result.depth.size() == size_t(size) * size &&
      result.serial > 0 && matched;
  std::printf("draw_modes_window size=%d stamp=%u offscreen=%d window=%d matched=%d serial=%llu result=%d\n",
              size, stamp, int(offscreenStatus), int(windowStatus), int(matched),
              static_cast<unsigned long long>(result.serial), int(good));
  return good;
}

static bool exercise(Display * display, Window window,
                     const CoinRenderNativeSurfaceDescriptor & native,
                     SoRenderManager::RenderMode mode) {
  Scene base = quad(.8f, 0, SbColor(1,0,0));
  Scene overlay = quad(.4f, -.5f, SbColor(0,1,0));
  SoRenderManager manager;
  manager.setSceneGraph(base.root);
  manager.setViewportRegion(SbViewportRegion(64,64));
  manager.setRenderMode(mode);
  CoinRenderOptions options;
  CoinRenderManagerAdapter offscreen(manager, SbVec2i32(64,64), options);
  CoinRenderManagerAdapter presented(manager, native, SbVec2i32(64,64), options);
  CoinRenderTarget * offTarget = offscreen.getSceneManager()->getRenderTarget();
  if (!offTarget || !offTarget->setDepthReadbackEnabled(TRUE)) return false;
  const int depthFlags = SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer = manager.addSuperimposition(overlay.root, depthFlags);
  layer->setEnabled(FALSE);
  bool ok = true;
  unsigned stamp = unsigned(mode == SoRenderManager::HIDDEN_LINE ? 32 : 96);
  bool firstSize = true;
  for (int size : {64,96,64}) {
    if (!firstSize) {
      manager.removeSuperimposition(layer);
      layer = manager.addSuperimposition(overlay.root, depthFlags);
      layer->setEnabled(FALSE);
    }
    firstSize = false;
    if (size != 64 || stamp > unsigned(mode == SoRenderManager::HIDDEN_LINE ? 32 : 96))
      ok &= resize(display, window, offscreen, presented, manager, size);
    if (!ok) break;
    Frame baseline, rear, front, clear;
    std::vector<uint8_t> visible;
    layer->setEnabled(FALSE);
    const unsigned baselineStamp = stamp++;
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     baselineStamp, baseline, visible);
    layer->setEnabled(TRUE);
    setZ(overlay, .4f, -.5f);
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     stamp++, rear, visible);
    setZ(overlay, .4f, .5f);
    const unsigned frontStamp = stamp++;
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     frontStamp, front, visible);
    manager.removeSuperimposition(layer);
    setZ(overlay, .4f, -.5f);
    layer = manager.addSuperimposition(overlay.root, depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    const unsigned clearStamp = stamp++;
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     clearStamp, clear, visible);
    if (!ok) break;
    const size_t center = size_t(size/2) * size + size/2;
    const bool rearOrdered = baseline.depth[center] < 1.0f ?
        rear.depth[center] == baseline.depth[center] :
        rear.depth[center] < 1.0f && greenCenter(rear, size);
    const size_t frontContent = changedContentInCenter(
        baseline, baselineStamp, front, frontStamp, size);
    const size_t clearContent = changedContentInCenter(
        baseline, baselineStamp, clear, clearStamp, size);
    const bool ordered = rearOrdered &&
        front.depth[center] < baseline.depth[center] &&
        clear.depth[center] > front.depth[center] &&
        greenCenter(front, size) && greenCenter(clear, size) &&
        frontContent > 0 && clearContent > 0;
    uint32_t baseBits = 0, rearBits = 0;
    std::memcpy(&baseBits, &baseline.depth[center], sizeof(baseBits));
    std::memcpy(&rearBits, &rear.depth[center], sizeof(rearBits));
    std::printf("draw_modes_window mode=%d size=%d ordered=%d rear_ordered=%d front_content=%zu clear_content=%zu depth_base=%.9g(0x%08x) rear=%.9g(0x%08x) front=%.9g clear=%.9g\n",
                int(mode), size, int(ordered), int(rearOrdered),
                frontContent, clearContent, baseline.depth[center], baseBits,
                rear.depth[center], rearBits, front.depth[center], clear.depth[center]);
    ok &= ordered;
  }
  if (ok) {
    const uint64_t stableSerial = offTarget->getLastSubmissionSerial();
    const uint64_t windowSerial = presented.getSceneManager()->getRenderTarget()->getLastSubmissionSerial();
    Frame stable;
    offTarget->readbackRGBA(stable.color);
    offTarget->readbackDepth(stable.depth);
    const std::vector<uint8_t> stableWindow = captureWindow(display, window, 64);
    overlay.alpha->function = SoAlphaTest::GREATER; overlay.alpha->value = .5f;
    manager.setBackgroundColor(SbColor4f(233.0f/255.0f, 17.0f/255.0f, 29.0f/255.0f, 1));
    const auto offStatus = offscreen.render();
    const auto winStatus = presented.render();
    Frame rejected;
    offTarget->readbackRGBA(rejected.color);
    offTarget->readbackDepth(rejected.depth);
    const std::vector<uint8_t> rejectedWindow = captureWindow(display, window, 64);
    const bool atomic = offStatus == CoinRenderAction::UNSUPPORTED &&
        winStatus == CoinRenderAction::UNSUPPORTED &&
        offTarget->getLastSubmissionSerial() == stableSerial &&
        presented.getSceneManager()->getRenderTarget()->getLastSubmissionSerial() == windowSerial &&
        rejected.color == stable.color && rejected.depth == stable.depth &&
        rejectedWindow == stableWindow;
    overlay.alpha->function = SoAlphaTest::NONE;
    Frame recovered; std::vector<uint8_t> recoveredWindow;
    const bool retry = renderPair(manager, offscreen, presented, display, window, 64,
                                  233, recovered, recoveredWindow) &&
                       recovered.serial > stableSerial;
    std::printf("draw_modes_window mode=%d alpha_test_atomic=%d retry=%d statuses=%d/%d\n",
                int(mode), int(atomic), int(retry), int(offStatus), int(winStatus));
    ok &= atomic && retry;
  }
  manager.removeSuperimposition(layer);
  overlay.root->unref(); base.root->unref();
  return ok;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  Display * display = XOpenDisplay(nullptr);
  if (!display) return 77;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      32,32,64,64,0,0,0);
  XSetWindowAttributes attributes{}; attributes.override_redirect = True;
  XChangeWindowAttributes(display, window, CWOverrideRedirect, &attributes);
  XMapRaised(display, window); XSync(display, False);
  CoinRenderNativeSurfaceDescriptor native{};
  native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
  native.structSize = sizeof(native);
  native.type = COIN_RENDER_SURFACE_XLIB;
  native.native.xlib.display = display;
  native.native.xlib.window = window;
  bool passed = true;
  for (auto mode : {SoRenderManager::WIREFRAME, SoRenderManager::POINTS,
                    SoRenderManager::BOUNDING_BOX, SoRenderManager::WIREFRAME_OVERLAY})
    passed = exercise(display, window, native, mode) && passed;
  XDestroyWindow(display, window); XCloseDisplay(display);
  return passed ? 0 : 1;
}
