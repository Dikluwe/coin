#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/rendering/CoinRenderManagerAdapter.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/nodes/SoAlphaTest.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

struct Scene { SoSeparator * root; SoCoordinate3 * points; SoMaterial * material; SoAlphaTest * alpha; };
struct Frame { std::vector<uint8_t> color; std::vector<float> depth; uint64_t serial = 0; };
enum class Profile { ScreenDoor, TextureBlend, MaterialTextureBlend };
static const char * profileName(Profile profile) {
  switch (profile) {
  case Profile::ScreenDoor: return "material_screen_door";
  case Profile::TextureBlend: return "texture_blend";
  case Profile::MaterialTextureBlend: return "material_texture_blend";
  }
  return "invalid";
}

static Scene quad(float extentX, float extentY, float z, const SbColor & color) {
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
    SbVec3f(-extentX,-extentY,z), SbVec3f(extentX,-extentY,z),
    SbVec3f(extentX,extentY,z), SbVec3f(-extentX,extentY,z)
  };
  points->point.setValues(0, 4, corners);
  root->addChild(points);
  auto * face = new SoFaceSet; face->numVertices.set1Value(0, 4);
  root->addChild(face);
  return {root, points, material, alpha};
}

static void setZ(Scene & scene, float extentX, float extentY, float z) {
  const SbVec3f corners[] = {
    SbVec3f(-extentX,-extentY,z), SbVec3f(extentX,-extentY,z),
    SbVec3f(extentX,extentY,z), SbVec3f(-extentX,extentY,z)
  };
  scene.points->point.setValues(0, 4, corners);
}

static void addVariableAlphaTexture(Scene & scene) {
  auto * texture = new SoTexture2;
  texture->model = SoTexture2::MODULATE;
  texture->wrapS = SoTexture2::CLAMP;
  texture->wrapT = SoTexture2::CLAMP;
  // Four alpha levels separate texture sampling from material transparency.
  const uint8_t texels[] = {255,255,255,0, 255,255,255,85,
                            255,255,255,170, 255,255,255,255};
  texture->image.setValue(SbVec2s(2,2),4,texels);
  scene.root->insertChild(texture,4);
  auto * uv = new SoTextureCoordinate2;
  const SbVec2f corners[] = {{0,0},{1,0},{1,1},{0,1}};
  uv->point.setValues(0,4,corners);
  scene.root->insertChild(uv,5);
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

struct DepthEvidence {
  size_t frontBlue=0,clearBlue=0,frontOverBase=0,clearOverBase=0;
  size_t baseCovered=0,baseWithFrontBlue=0,baseWithClearBlue=0;
  size_t frontCloser=0,clearFarther=0,rearSame=0,rearNear=0;
  float maxRearDepthDelta=0,maxFrontDepthDelta=0,maxClearDepthDelta=0;
};
static DepthEvidence depthEvidence(const Frame & baseline,const Frame & rear,
                                   const Frame & front,const Frame & clear,int size) {
  DepthEvidence found;
  if (baseline.depth.size()!=size_t(size)*size || rear.depth.size()!=baseline.depth.size() ||
      front.depth.size()!=baseline.depth.size() || clear.depth.size()!=baseline.depth.size()) return found;
  for (size_t p=0;p<baseline.depth.size();++p) {
    const int baseBlue=baseline.color[p*4+2];
    const bool frontBlue=int(front.color[p*4+2])>baseBlue+16;
    const bool clearBlue=int(clear.color[p*4+2])>baseBlue+16;
    found.frontBlue+=frontBlue;
    found.clearBlue+=clearBlue;
    const bool baseCovered=baseline.depth[p]<1.0f;
    found.baseCovered+=baseCovered;
    found.baseWithFrontBlue+=baseCovered && frontBlue;
    found.baseWithClearBlue+=baseCovered && clearBlue;
    found.frontCloser+=baseCovered && front.depth[p]+1e-6f<baseline.depth[p];
    found.clearFarther+=baseCovered && clear.depth[p]>baseline.depth[p]+1e-6f && clear.depth[p]<1.0f;
    found.rearSame+=baseCovered && rear.depth[p]==baseline.depth[p];
    found.rearNear+=baseCovered && std::abs(rear.depth[p]-baseline.depth[p])<=1e-6f;
    if (baseCovered) {
      found.maxRearDepthDelta=std::max(found.maxRearDepthDelta,std::abs(rear.depth[p]-baseline.depth[p]));
      found.maxFrontDepthDelta=std::max(found.maxFrontDepthDelta,std::abs(front.depth[p]-baseline.depth[p]));
      found.maxClearDepthDelta=std::max(found.maxClearDepthDelta,std::abs(clear.depth[p]-baseline.depth[p]));
    }
    found.frontOverBase+=baseCovered && frontBlue &&
      front.depth[p]+1e-6f<baseline.depth[p] && rear.depth[p]==baseline.depth[p];
    found.clearOverBase+=baseCovered && clearBlue &&
      clear.depth[p]>baseline.depth[p]+1e-6f && clear.depth[p]<1.0f &&
      rear.depth[p]==baseline.depth[p];
  }
  return found;
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
  CoinRenderTarget * target = offscreen.getSceneManager()->getRenderTarget();
  CoinRenderTarget * windowTarget = presented.getSceneManager()->getRenderTarget();
  const uint64_t priorOffSerial=target->getLastSubmissionSerial();
  const uint64_t priorWindowSerial=windowTarget->getLastSubmissionSerial();
  manager.setBackgroundColor(SbColor4f(float(stamp)/255.0f, 17.0f/255.0f,
                                       29.0f/255.0f, 1));
  const auto offscreenStatus = offscreen.render();
  const auto windowStatus = presented.render();
  target->readbackRGBA(result.color);
  target->readbackDepth(result.depth);
  result.serial = target->getLastSubmissionSerial();
  const bool matched = waitMatch(display, window, size, result, visible);
  const bool good = offscreenStatus == CoinRenderAction::SUCCESS &&
      windowStatus == CoinRenderAction::SUCCESS &&
      result.color.size() == size_t(size) * size * 4u &&
      result.depth.size() == size_t(size) * size &&
      result.serial > priorOffSerial &&
      windowTarget->getLastSubmissionSerial()>priorWindowSerial && matched;
  std::printf("technical_alpha_window size=%d stamp=%u offscreen=%d window=%d matched=%d serial=%llu/%llu result=%d\n",
              size, stamp, int(offscreenStatus), int(windowStatus), int(matched),
              static_cast<unsigned long long>(result.serial),
              static_cast<unsigned long long>(windowTarget->getLastSubmissionSerial()),int(good));
  return good;
}

static bool exercise(Display * display, Window window,
                     const CoinRenderNativeSurfaceDescriptor & native,
                     SoRenderManager::RenderMode mode,Profile profile) {
  Scene base = quad(.8f, .75f, 0, SbColor(1,0,0));
  Scene overlay = quad(1.0f, 1.0f, -.5f, SbColor(0,0,1));
  overlay.material->transparency = profile==Profile::TextureBlend ? 0.0f : .5f;
  if (profile!=Profile::ScreenDoor) addVariableAlphaTexture(overlay);
  const auto transparency=profile==Profile::ScreenDoor ?
    SoGLRenderAction::SCREEN_DOOR : SoGLRenderAction::BLEND;
  SoRenderManager manager;
  manager.setSceneGraph(base.root);
  manager.setViewportRegion(SbViewportRegion(64,64));
  manager.setRenderMode(mode);
  manager.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  CoinRenderOptions options;
  CoinRenderManagerAdapter offscreen(manager, SbVec2i32(64,64), options);
  CoinRenderManagerAdapter presented(manager, native, SbVec2i32(64,64), options);
  CoinRenderTarget * offTarget = offscreen.getSceneManager()->getRenderTarget();
  if (!offTarget || !offTarget->setDepthReadbackEnabled(TRUE)) return false;
  const int depthFlags = SoRenderManager::Superimposition::AUTOREDRAW |
                         SoRenderManager::Superimposition::ZBUFFERON;
  auto * layer = manager.addSuperimposition(overlay.root, depthFlags);
  layer->setTransparencyType(transparency);
  layer->setEnabled(FALSE);
  bool ok = true;
  unsigned stamp = unsigned(mode == SoRenderManager::HIDDEN_LINE ? 32 : 96);
  bool firstSize = true;
  for (int size : {64,96,65,64}) {
    if (!firstSize) {
      manager.removeSuperimposition(layer);
      layer = manager.addSuperimposition(overlay.root, depthFlags);
      layer->setTransparencyType(transparency);
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
    setZ(overlay, 1.0f, 1.0f, -.5f);
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     stamp++, rear, visible);
    setZ(overlay, 1.0f, 1.0f, .5f);
    const unsigned frontStamp = stamp++;
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     frontStamp, front, visible);
    manager.removeSuperimposition(layer);
    setZ(overlay, 1.0f, 1.0f, -.5f);
    layer = manager.addSuperimposition(overlay.root, depthFlags |
      SoRenderManager::Superimposition::CLEARZBUFFER);
    layer->setTransparencyType(transparency);
    const unsigned clearStamp = stamp++;
    ok &= renderPair(manager, offscreen, presented, display, window, size,
                     clearStamp, clear, visible);
    if (!ok) break;
    const DepthEvidence evidence=depthEvidence(baseline,rear,front,clear,size);
    const bool ordered=evidence.frontBlue>0 && evidence.clearBlue>0 &&
      evidence.frontOverBase>0 && evidence.clearOverBase>0;
    std::printf("technical_alpha_window mode=%d profile=%s size=%d ordered=%d front_blue=%zu clear_blue=%zu front_over_base=%zu clear_over_base=%zu base_covered=%zu base_front_blue=%zu base_clear_blue=%zu front_closer=%zu clear_farther=%zu rear_same=%zu rear_near=%zu max_depth_delta_rear=%.9g front=%.9g clear=%.9g\n",
                int(mode),profileName(profile),size,int(ordered),evidence.frontBlue,
                evidence.clearBlue,evidence.frontOverBase,evidence.clearOverBase,
                evidence.baseCovered,evidence.baseWithFrontBlue,evidence.baseWithClearBlue,
                evidence.frontCloser,evidence.clearFarther,evidence.rearSame,evidence.rearNear,
                evidence.maxRearDepthDelta,evidence.maxFrontDepthDelta,evidence.maxClearDepthDelta);
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
    std::printf("technical_alpha_window mode=%d profile=%s alpha_test_atomic=%d retry=%d statuses=%d/%d\n",
                int(mode), profileName(profile),int(atomic), int(retry), int(offStatus), int(winStatus));
    ok &= atomic && retry;
  }
  manager.removeSuperimposition(layer);
  overlay.root->unref(); base.root->unref();
  return ok;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(COIN_RENDER_EXPERIMENTAL_XLIB_WINDOW,
                                     &caps,sizeof(caps))!=0) return 77;
  std::printf("technical_alpha_window_runtime backend=%u renderer=%u vendor=0x%x device=0x%x adapter=%s\n",
              unsigned(caps.backend),unsigned(caps.renderer),unsigned(caps.vendor_id),
              unsigned(caps.device_id),caps.adapter_name);
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
  for (auto profile : {Profile::ScreenDoor,Profile::TextureBlend,Profile::MaterialTextureBlend})
    for (auto mode : {SoRenderManager::WIREFRAME, SoRenderManager::POINTS,
                      SoRenderManager::BOUNDING_BOX, SoRenderManager::WIREFRAME_OVERLAY,
                      SoRenderManager::HIDDEN_LINE, SoRenderManager::SHADED_HIDDEN_LINES})
      passed = exercise(display, window, native, mode,profile) && passed;
  XDestroyWindow(display, window); XCloseDisplay(display);
  return passed ? 0 : 1;
}
