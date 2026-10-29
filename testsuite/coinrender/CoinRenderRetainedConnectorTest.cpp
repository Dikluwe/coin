#include "config.h"
#include "CoinRenderRetainedTestRenderer.h"
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/misc/SoContextHandler.h>
#include <Inventor/system/gl.h>
#ifdef HAVE_EGL
#include <EGL/egl.h>
#endif
#include <Inventor/rendering/CoinRenderOptions.h>
#include <Inventor/actions/SoIRRenderAction.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture3.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include "rendering/coinrender/CoinRenderRetainedTransport.h"
#include "rendering/coinrender/CoinRenderComposition.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>

namespace {
bool renderGL(SoNode * scene, const SbViewportRegion & viewport, std::vector<uint8_t> & pixels) {
#ifdef HAVE_EGL
  // Own the reference context explicitly. This avoids dependence on an X11
  // pixmap/context policy and exercises the actual SoGLRenderAction renderer.
  const EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) return false;
  struct DisplayScope { EGLDisplay display; ~DisplayScope() { eglTerminate(display); } } displayScope{display};
  if (!eglBindAPI(EGL_OPENGL_API)) return false;
  const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1) return false;
  const EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
  if (context == EGL_NO_CONTEXT) return false;
  const EGLint extent[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  const EGLSurface surface = eglCreatePbufferSurface(display, config, extent);
  if (surface == EGL_NO_SURFACE) { eglDestroyContext(display, context); return false; }
  struct ContextScope {
    EGLDisplay display; EGLSurface surface; EGLContext context;
    ~ContextScope() {
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroySurface(display, surface); eglDestroyContext(display, context);
    }
  } contextScope{display, surface, context};
  if (!eglMakeCurrent(display, surface, surface, context)) return false;
  const uint32_t cache = SoGLCacheContextElement::getUniqueCacheContext();
  {
    SoGLRenderAction action(viewport);
    action.setCacheContext(cache);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 0, 1); glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    action.apply(scene);
    pixels.resize(64 * 64 * 3);
    glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  }
  SoContextHandler::destructingContext(cache);
  std::cout << "GL reference renderer=" << reinterpret_cast<const char *>(glGetString(GL_RENDERER)) << '\n';
  return glGetError() == GL_NO_ERROR;
#else
  SoOffscreenRenderer reference(viewport);
  reference.setComponents(SoOffscreenRenderer::RGB);
  if (!reference.render(scene) || !reference.getBuffer()) return false;
  pixels.assign(reference.getBuffer(), reference.getBuffer() + 64 * 64 * 3);
  return true;
#endif
}
int failures = 0;
void check(bool condition, const char * message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool centerIs(CoinRenderTarget & target, int channel) {
  std::vector<uint8_t> pixels;
  target.readbackRGBA(pixels);
  const size_t at = (32 * 64 + 32) * 4;
  if (pixels.size() != 64 * 64 * 4) return false;
  if (pixels[at + channel] < 240) {
    std::cerr << "center RGBA=" << int(pixels[at]) << ',' << int(pixels[at+1]) << ','
              << int(pixels[at+2]) << ',' << int(pixels[at+3]) << '\n';
    return false;
  }
  for (int c = 0; c < 3; ++c) if (c != channel && pixels[at + c] > 10) return false;
  return true;
}
}

int main(int argc, char ** argv) {
  SoDB::init();
  const SbViewportRegion viewport(64, 64);
  CoinRenderOptions options;
#if defined(HAVE_COIN_BGFX) || defined(HAVE_COIN_WGPU_RUST_BRIDGE)
  options.renderer = COIN_RENDER_RENDERER_VULKAN;
#endif
  if (!coinRetainedTestRenderer(options)) return 1;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64), options));
  check(target.get() != nullptr, "offscreen target");
  if (!target) return 1;
  auto * scene = new SoSeparator;
  scene->ref();
  auto * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 5);
  camera->nearDistance = 1; camera->farDistance = 10; camera->height = 4;
  scene->addChild(camera);
  auto * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  scene->addChild(model);
  auto * red = new SoMaterial;
  red->diffuseColor.setValue(1, 0, 0);
  scene->addChild(red);
  scene->addChild(new SoCube);
  auto * green = new SoMaterial;
  green->diffuseColor.setValue(0, 1, 0);
  scene->addChild(green);
  scene->addChild(new SoCube);

  CoinRenderRetainedRenderer renderer(viewport);
#ifdef COIN_WGPU_TEST_HOOKS
  const auto classifications = coin_render_composition_classification_count_for_tests();
#endif
  auto status = renderer.render(scene, *target);
  if (status != CoinRenderAction::SUCCESS) std::cerr << renderer.getLastError().getString() << '\n';
  check(status == CoinRenderAction::SUCCESS, "native retained capture reaches selected transport");
  check(renderer.getCaptureCount() == 1 && renderer.getPlanBuildCount() == 1, "one capture and one plan");
#ifdef COIN_WGPU_TEST_HOOKS
  check(coin_render_composition_classification_count_for_tests() == classifications,
        "native plan bypasses CoinRender composition classification");
#endif
  check(renderer.getDrawList().getNumCommands() == 2, "two retained occurrences");
  check(centerIs(*target, 1), "second equal-depth cube wins in upstream plan order");
  std::vector<float> depth;
  target->readbackDepth(depth);
  check(depth.size() == 64 * 64 && std::abs(depth[32 * 64 + 32] - 1.0f / 3.0f) < 0.00001f,
        "Coin projection reaches transport with correct normalized depth");

  // A valid externally resolved order intentionally differs from insertion
  // order. Reclassification/sorting by a second planner would turn this green.
  SoRenderPlan reverse = renderer.getRenderPlan();
  std::vector<int> draws;
  for (int i = 0; i < reverse.getNumOperations(); ++i)
    if (reverse.getOperation(i).type == SoRenderOperationType::DRAW) draws.push_back(i);
  check(draws.size() == 2, "native plan draw operations");
  if (draws.size() == 2) {
    auto & a = const_cast<SoRenderOperation &>(reverse.getOperation(draws[0]));
    auto & b = const_cast<SoRenderOperation &>(reverse.getOperation(draws[1]));
    std::swap(a.commandIndex, b.commandIndex);
    CoinRenderRetainedTransport transport(*target);
    transport.initialize({});
    SoRenderParams params;
    for(int c=0;c<4;++c)check(params.clearColor[c]==0,"default retained clear color is initialized");
    params.viewport = viewport;
    params.flags = SO_PARAM_CLEAR_WINDOW | SO_PARAM_CLEAR_DEPTH;
    const bool executed = transport.render(renderer.getDrawList(), reverse, params);
    if (!executed) std::cerr << transport.getResult().diagnostic << '\n';
    check(executed, "execute external resolved plan");
    check(centerIs(*target, 0), "transport honors external order without replanning");
#ifdef COIN_WGPU_TEST_HOOKS
    check(coin_render_composition_classification_count_for_tests() == classifications,
          "external plan also bypasses second planner");
#endif
    // External plans can change in place without advancing capture revisions.
    // They must never acquire the renderer-owned transport reuse authority.
    std::swap(a.commandIndex, b.commandIndex);
    check(transport.render(renderer.getDrawList(), reverse, params), "resubmit edited external plan");
    check(centerIs(*target, 1), "external plan edit is observed without a capture revision");
    std::swap(a.commandIndex, b.commandIndex);
    check(transport.render(renderer.getDrawList(), reverse, params), "restore external plan order");
    check(centerIs(*target, 0), "restored external plan order is observed");
    const auto serial = target->getLastSubmissionSerial();
    a.commandIndex = b.commandIndex;
    check(!transport.render(renderer.getDrawList(), reverse, params), "duplicate plan command rejected");
    check(target->getLastSubmissionSerial() == serial && centerIs(*target, 0), "invalid plan preserves publication");
  }

  std::vector<uint8_t> previous;
  target->readbackRGBA(previous);
  const auto serial = target->getLastSubmissionSerial();
  const auto reject = [&](SoNode * unsupported, const char * message) {
    scene->insertChild(unsupported, 2);
    const auto plans = renderer.getPlanBuildCount();
    check(renderer.render(scene, *target) == CoinRenderAction::UNSUPPORTED, message);
    check(renderer.getPlanBuildCount() == plans, "reject during same capture, before planning");
    std::vector<uint8_t> current;
    target->readbackRGBA(current);
    check(current == previous && target->getLastSubmissionSerial() == serial, "unsupported capture preserves image and serial");
    scene->removeChild(unsupported);
  };
  reject(new SoTexture3, "unsupported volume texture cannot disappear from IR silently");
  auto * invalidUnit = new SoTextureUnit;
  invalidUnit->unit = 8;
  reject(invalidUnit, "out-of-range texture unit cannot disappear from IR silently");
  green->transparency = 0.5f;
  renderer.setTransparencyType(CoinRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
  check(renderer.render(scene, *target) == CoinRenderAction::UNSUPPORTED, "unrepresented transparency policy rejected");
  check(target->getLastSubmissionSerial() == serial, "transparent refusal preserves serial");
  renderer.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  green->transparency = 0;
  model->model = SoLightModel::PHONG;
  check(renderer.render(scene, *target) == CoinRenderAction::SUCCESS, "opaque PHONG lighting is connected");
  model->model = SoLightModel::BASE_COLOR;
  check(renderer.render(scene, *target) == CoinRenderAction::SUCCESS && centerIs(*target, 1), "valid frame recovers after refusals");
  if (argc == 2 && std::strcmp(argv[1], "--gl-reference") == 0) {
    std::vector<uint8_t> reference;
    check(renderGL(scene, viewport, reference), "current Coin OpenGL reference is required");
    if (!reference.empty()) {
      const uint8_t * gl = reference.data();
      std::vector<uint8_t> pixels;
      target->readbackRGBA(pixels);
      size_t compared = 0, different = 0;
      for (int y = 1; y < 63; ++y) for (int x = 1; x < 63; ++x) {
        const size_t at = static_cast<size_t>(y * 64 + x);
        bool flat = true;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
          const size_t adjacent = static_cast<size_t>((y + dy) * 64 + x + dx);
          for (int c = 0; c < 3; ++c)
            flat = flat && gl[at * 3 + c] == gl[adjacent * 3 + c] &&
              pixels[at * 4 + c] == pixels[adjacent * 4 + c];
        }
        if (!flat) continue; // Fixed one-pixel boundary exclusion for raster coverage.
        ++compared;
        for (int c = 0; c < 3; ++c)
          if (std::abs(int(gl[at * 3 + c]) - int(pixels[at * 4 + c])) > 2) ++different;
      }
      check(compared > 3000 && different == 0, "flat interiors match current Coin OpenGL reference");
      std::cout << "GL reference: compared=" << compared << " differing_channels=" << different << '\n';
    }
  }
  scene->unref();
  return failures ? 1 : 0;
}
