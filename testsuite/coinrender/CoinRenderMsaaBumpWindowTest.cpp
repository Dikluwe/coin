#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinwgpu/CoinWgpuFfi.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef Status
#undef Status
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

struct Pixel { uint8_t r, g, b; };
struct Fixture { SoSeparator * root; SoMaterial * material; SoTexture2 * texture; };

static uint8_t channel(unsigned long value, unsigned long mask) {
  if (!mask) return 0;
  while (!(mask & 1)) { mask >>= 1; value >>= 1; }
  return static_cast<uint8_t>(((value & mask) * 255 + mask / 2) / mask);
}

static std::vector<Pixel> windowPixels(Display * display, Window window, int size) {
  XSync(display, False);
  XImage * image = XGetImage(display, window, 0, 0, size, size, AllPlanes, ZPixmap);
  if (!image) return {};
  std::vector<Pixel> result(size_t(size) * size);
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const unsigned long value = XGetPixel(image, x, y);
    result[size_t(y) * size + x] = {channel(value, image->red_mask),
                                    channel(value, image->green_mask),
                                    channel(value, image->blue_mask)};
  }
  XDestroyImage(image);
  return result;
}

static std::vector<Pixel> offscreenPixels(SoSeparator * root, int size,
                                          uint32_t samples, SbColor4f background) {
  CoinRenderOptions options;
  options.sampleCount = samples;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createOffscreen(SbVec2i32(size, size), options));
  if (!target || !target->setDepthReadbackEnabled(FALSE)) return {};
  CoinRenderAction action(SbViewportRegion(size, size));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setBackgroundColor(background);
  action.setRenderTarget(target.get());
  action.apply(root);
  std::vector<uint8_t> rgba;
  target->readbackRGBA(rgba);
  if (action.getLastStatus() != CoinRenderAction::SUCCESS ||
      rgba.size() != size_t(size) * size * 4u) {
    std::fprintf(stderr, "bump window offscreen reference failed: %s\n",
                 action.getLastError().getString());
    return {};
  }
  std::vector<Pixel> result(size_t(size) * size);
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = {rgba[4u*i], rgba[4u*i+1u], rgba[4u*i+2u]};
  action.setRenderTarget(nullptr);
  return result;
}

static bool matches(const std::vector<Pixel> & observed,
                    const std::vector<Pixel> & reference, int size,
                    unsigned & maxDelta, size_t & changed) {
  if (observed.size() != size_t(size) * size || observed.size() != reference.size())
    return false;
  unsigned maxNormal = 0, maxFlipped = 0;
  size_t changedNormal = 0, changedFlipped = 0;
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const Pixel a = observed[size_t(y)*size+x];
    const Pixel b = reference[size_t(y)*size+x];
    const Pixel f = reference[size_t(size-1-y)*size+x];
    const unsigned normal = std::max({unsigned(std::abs(int(a.r)-int(b.r))),
                                      unsigned(std::abs(int(a.g)-int(b.g))),
                                      unsigned(std::abs(int(a.b)-int(b.b)))});
    const unsigned flipped = std::max({unsigned(std::abs(int(a.r)-int(f.r))),
                                       unsigned(std::abs(int(a.g)-int(f.g))),
                                       unsigned(std::abs(int(a.b)-int(f.b)))});
    maxNormal = std::max(maxNormal, normal);
    maxFlipped = std::max(maxFlipped, flipped);
    changedNormal += normal > 4;
    changedFlipped += flipped > 4;
  }
  if (changedNormal <= changedFlipped) {
    maxDelta = maxNormal; changed = changedNormal;
  } else {
    maxDelta = maxFlipped; changed = changedFlipped;
  }
  return changed == 0;
}

static bool waitForMatch(Display * display, Window window, int size,
                         const std::vector<Pixel> & reference,
                         std::vector<Pixel> & observed) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  unsigned maxDelta = 255; size_t changed = size_t(size) * size;
  do {
    observed = windowPixels(display, window, size);
    if (matches(observed, reference, size, maxDelta, changed)) {
      std::printf("msaa_bump_window match size=%d max_delta=%u mismatched=%zu\n",
                  size, maxDelta, changed);
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  std::fprintf(stderr, "msaa_bump_window present mismatch size=%d max_delta=%u mismatched=%zu\n",
               size, maxDelta, changed);
  return false;
}

static Fixture makeFixture(unsigned variant) {
  auto * root = new SoSeparator; root->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4); camera->height = 4;
  root->addChild(camera);
  auto * light = new SoDirectionalLight;
  light->direction.setValue(0, 0, -1);
  root->addChild(light);
  auto * hints = new SoShapeHints;
  hints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
  root->addChild(hints);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(.8f, .2f, .1f);
  if (variant == 2) {
    material->specularColor.setValue(.6f, .6f, .6f);
    material->shininess = .2f;
  }
  root->addChild(material);
  auto * bump = new SoBumpMap;
  const unsigned char normal[] = {128, 128, 255};
  bump->image.setValue(SbVec2s(1, 1), 3, normal);
  root->addChild(bump);
  SoTexture2 * texture = nullptr;
  if (variant == 1) {
    texture = new SoTexture2;
    const unsigned char opaque[] = {128, 128, 128, 255};
    texture->image.setValue(SbVec2s(1, 1), 4, opaque);
    texture->model = SoTexture2::MODULATE;
    root->addChild(texture);
  }
  auto * rotation = new SoRotation;
  rotation->rotation.setValue(SbVec3f(0, 0, 1), .23f);
  root->addChild(rotation);
  root->addChild(new SoCube);
  return {root, material, texture};
}

static bool resizeWindow(Display * display, Window window,
                         CoinRenderTarget * target, CoinRenderAction & action,
                         int size) {
  XResizeWindow(display, window, size, size); XSync(display, False);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  XWindowAttributes actual{};
  do {
    XGetWindowAttributes(display, window, &actual);
    if (actual.width == size && actual.height == size) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  if (actual.width != size || actual.height != size ||
      !target->resize(SbVec2i32(size, size))) return false;
  action.setViewportRegion(SbViewportRegion(size, size));
  return true;
}

static bool exercise(Display * display, Window window,
                     const CoinRenderNativeSurfaceDescriptor & native,
                     unsigned variant, uint32_t samples) {
  Fixture fixture = makeFixture(variant);
  CoinRenderOptions options;
  options.sampleCount = samples;
  std::unique_ptr<CoinRenderTarget> target(
      CoinRenderTarget::createWindow(native, SbVec2i32(64, 64), options));
  if (!target || target->getStatus() != CoinRenderTarget::TARGET_READY) {
    fixture.root->unref(); return false;
  }
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setTransparencyType(CoinRenderAction::NONE);
  action.setRenderTarget(target.get());
  bool passed = true;
  std::vector<Pixel> observed, baseline;
  for (int size : {64, 96, 64}) {
    if (size != 64 || target->getLastSubmissionSerial() > 0)
      passed &= resizeWindow(display, window, target.get(), action, size);
    const SbColor4f background(0, 17.0f/255.0f, 29.0f/255.0f, 1);
    action.setBackgroundColor(background);
    const auto reference = offscreenPixels(fixture.root, size, samples, background);
    action.apply(fixture.root);
    const bool rendered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
                          target->getLastSubmissionSerial() > 0;
    passed &= rendered && waitForMatch(display, window, size, reference, observed);
    if (!passed) {
      std::fprintf(stderr, "msaa_bump_window variant=%u samples=%u size=%d status=%d error=%s\n",
                   variant, samples, size, int(action.getLastStatus()),
                   action.getLastError().getString());
      break;
    }
    const Pixel center = observed[size_t(size/2)*size+size/2];
    const Pixel expected = reference[size_t(size/2)*size+size/2];
    passed &= std::abs(int(center.r)-int(expected.r)) <= 4 &&
              std::abs(int(center.g)-int(expected.g)) <= 4 &&
              std::abs(int(center.b)-int(expected.b)) <= 4;
    std::printf("msaa_bump_window variant=%u samples=%u size=%d center=%u,%u,%u serial=%llu\n",
                variant, samples, size, unsigned(center.r), unsigned(center.g),
                unsigned(center.b),
                static_cast<unsigned long long>(target->getLastSubmissionSerial()));
  }
  if (passed && samples > 1) {
    const uint64_t serial = target->getLastSubmissionSerial();
    const auto stable = observed;
    fixture.material->diffuseColor.setValue(.35f, .45f, .1f);
    const SbColor4f background(0, 17.0f/255.0f, 29.0f/255.0f, 1);
    const auto changedReference = offscreenPixels(fixture.root, 64, samples, background);
    const size_t centerIndex = 32u * 64u + 32u;
    const bool visiblyChanged = changedReference.size() == stable.size() &&
              (changedReference[centerIndex].r != stable[centerIndex].r ||
               changedReference[centerIndex].g != stable[centerIndex].g ||
               changedReference[centerIndex].b != stable[centerIndex].b);
    std::printf("msaa_bump_window changed_reference variant=%u samples=%u valid=%d old_center=%u,%u,%u new_center=%u,%u,%u\n",
                variant, samples, int(visiblyChanged),
                unsigned(stable[centerIndex].r), unsigned(stable[centerIndex].g),
                unsigned(stable[centerIndex].b),
                changedReference.size() == stable.size() ? unsigned(changedReference[centerIndex].r) : 0,
                changedReference.size() == stable.size() ? unsigned(changedReference[centerIndex].g) : 0,
                changedReference.size() == stable.size() ? unsigned(changedReference[centerIndex].b) : 0);
    passed &= visiblyChanged;
    coin_wgpu_inject_fault(COIN_WGPU_FAULT_MSAA_AFTER_ENCODE);
    action.apply(fixture.root);
    coin_wgpu_inject_fault(0);
    const auto afterFault = windowPixels(display, window, 64);
    unsigned maxDelta = 255; size_t changed = stable.size();
    const bool sameFrame = matches(afterFault, stable, 64, maxDelta, changed);
    const bool faultRejected = action.getLastStatus() == CoinRenderAction::OUT_OF_MEMORY &&
              target->getLastSubmissionSerial() == serial &&
              afterFault.size() == stable.size() && sameFrame;
    std::printf("msaa_bump_window fault307 variant=%u samples=%u rejected=%d status=%d serial=%llu->%llu frame_equal=%d max_delta=%u changed=%zu error=%s\n",
                variant, samples, int(faultRejected), int(action.getLastStatus()),
                static_cast<unsigned long long>(serial),
                static_cast<unsigned long long>(target->getLastSubmissionSerial()),
                int(sameFrame), maxDelta, changed, action.getLastError().getString());
    passed &= faultRejected;
    action.apply(fixture.root);
    const bool retryMatched = waitForMatch(display, window, 64, changedReference, observed);
    const bool retried = action.getLastStatus() == CoinRenderAction::SUCCESS &&
              target->getLastSubmissionSerial() > serial && retryMatched;
    std::printf("msaa_bump_window retry variant=%u samples=%u recovered=%d status=%d serial=%llu->%llu matched=%d error=%s\n",
                variant, samples, int(retried), int(action.getLastStatus()),
                static_cast<unsigned long long>(serial),
                static_cast<unsigned long long>(target->getLastSubmissionSerial()),
                int(retryMatched), action.getLastError().getString());
    passed &= retried;
  }
  if (passed && fixture.texture) {
    const auto stable = observed;
    const uint64_t serial = target->getLastSubmissionSerial();
    const unsigned char translucent[] = {128, 128, 128, 128};
    fixture.texture->image.setValue(SbVec2s(1, 1), 4, translucent);
    action.apply(fixture.root);
    const auto rejectedImage = windowPixels(display, window, 64);
    unsigned maxDelta = 255; size_t changed = stable.size();
    const bool sameFrame = matches(rejectedImage, stable, 64, maxDelta, changed);
    const bool rejected = action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
              target->getLastSubmissionSerial() == serial && sameFrame;
    std::printf("msaa_bump_window alpha_reject variant=%u samples=%u rejected=%d status=%d serial=%llu->%llu frame_equal=%d max_delta=%u changed=%zu error=%s\n",
                variant, samples, int(rejected), int(action.getLastStatus()),
                static_cast<unsigned long long>(serial),
                static_cast<unsigned long long>(target->getLastSubmissionSerial()),
                int(sameFrame), maxDelta, changed, action.getLastError().getString());
    passed &= rejected;
    const unsigned char opaque[] = {128, 128, 128, 255};
    fixture.texture->image.setValue(SbVec2s(1, 1), 4, opaque);
    action.apply(fixture.root);
    const SbColor4f background(0, 17.0f/255.0f, 29.0f/255.0f, 1);
    const auto reference = offscreenPixels(fixture.root, 64, samples, background);
    const bool recoveryMatched = waitForMatch(display, window, 64, reference, observed);
    const bool recovered = action.getLastStatus() == CoinRenderAction::SUCCESS &&
              target->getLastSubmissionSerial() > serial && recoveryMatched;
    std::printf("msaa_bump_window alpha_recovery variant=%u samples=%u recovered=%d status=%d serial=%llu->%llu matched=%d error=%s\n",
                variant, samples, int(recovered), int(action.getLastStatus()),
                static_cast<unsigned long long>(serial),
                static_cast<unsigned long long>(target->getLastSubmissionSerial()),
                int(recoveryMatched), action.getLastError().getString());
    passed &= recovered;
  }
  action.setRenderTarget(nullptr);
  fixture.root->unref();
  std::printf("msaa_bump_window variant=%u samples=%u passed=%d\n",
              variant, samples, int(passed));
  return passed;
}

int main() {
  SoDB::init(); CoinRenderAction::initClass();
  if (!CoinRenderAction::isGpuBackendAvailable()) return 77;
  Display * display = XOpenDisplay(nullptr);
  if (!display) return 77;
  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                      32, 32, 64, 64, 0, 0, 0);
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
  bool passed = true;
  for (unsigned variant = 0; variant < 3 && passed; ++variant)
    for (uint32_t samples : {1u, 2u, 4u})
      passed = exercise(display, window, native, variant, samples) && passed;
  XDestroyWindow(display, window); XCloseDisplay(display);
  return passed ? 0 : 1;
}
