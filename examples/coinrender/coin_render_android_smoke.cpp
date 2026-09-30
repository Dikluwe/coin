/* P23 NativeActivity smoke: the Android host owns ANativeWindow. Each
 * APP_CMD_INIT_WINDOW creates a new CoinRenderTarget; TERM_WINDOW destroys it
 * before the NDK invalidates the pointer. No parallel scene interpretation. */
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

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

#include <cstdint>
#include <memory>
#include <vector>

namespace {
const char * const tag = "CoinRenderP23";

uint64_t checksum(const std::vector<uint8_t> & bytes) {
  uint64_t value = UINT64_C(14695981039346656037);
  for (uint8_t byte : bytes) {
    value ^= byte;
    value *= UINT64_C(1099511628211);
  }
  return value;
}

struct Host {
  android_app * app = nullptr;
  SoSeparator * scene = nullptr;
  std::unique_ptr<CoinRenderAction> action;
  std::unique_ptr<CoinRenderTarget> target;
  uint64_t surfaceGeneration = 0;
  bool resumed = false;
  bool failed = false;

  void releaseSurface() {
    if (action) action->setRenderTarget(nullptr);
    target.reset();
  }

  void render() {
    if (!resumed || !target || !app->window) return;
    const SbVec2i32 size(ANativeWindow_getWidth(app->window),
                         ANativeWindow_getHeight(app->window));
    if (size[0] <= 0 || size[1] <= 0) {
      target->resize(SbVec2i32(0, 0));
      return;
    }
    if (size != target->getSize() && !target->resize(size)) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "resize failed: %s", target->getLastError());
      return;
    }
    action->setViewportRegion(SbViewportRegion(size[0], size[1]));
    if (!target->requestWindowReadbackRGBA()) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "capture request failed");
      return;
    }
    action->apply(scene);
    if (action->getLastStatus() != CoinRenderAction::SUCCESS) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "render failed: %s",
                          action->getLastError().getString());
      return;
    }
    std::vector<uint8_t> rgba;
    target->readbackRGBA(rgba);
    if (rgba.size() != size_t(size[0]) * size_t(size[1]) * 4u) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "incomplete RGBA capture");
      return;
    }
    const size_t center = (size_t(size[1] / 2) * size_t(size[0]) + size_t(size[0] / 2)) * 4u;
    if (rgba[center] < rgba[center + 1] + 50 || rgba[center] < rgba[center + 2] + 50) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "scene center is not red");
      return;
    }
    const uint64_t capturedSerial = target->getLastSubmissionSerial();
    action->apply(scene);
    std::vector<uint8_t> normalRgba;
    target->readbackRGBA(normalRgba);
    if (action->getLastStatus() != CoinRenderAction::SUCCESS || !normalRgba.empty() ||
        target->getLastSubmissionSerial() <= capturedSerial) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "normal frame/readback policy failed");
      return;
    }
    __android_log_print(ANDROID_LOG_INFO, tag,
      "generation=%llu serial=%llu size=%dx%d rgba_fnv64=%016llx",
      static_cast<unsigned long long>(surfaceGeneration),
      static_cast<unsigned long long>(target->getLastSubmissionSerial()),
      size[0], size[1], static_cast<unsigned long long>(checksum(rgba)));
  }

  void createSurface() {
    releaseSurface();
    if (!app->window) return;
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_ANDROID_NDK;
    native.native.android.nativeWindow = app->window;
    CoinRenderOptions options;
    options.renderer = COIN_RENDER_RENDERER_VULKAN;
    const SbVec2i32 size(ANativeWindow_getWidth(app->window),
                         ANativeWindow_getHeight(app->window));
    target.reset(CoinRenderTarget::createWindow(native, size, options));
    if (!target || target->getStatus() == CoinRenderTarget::TARGET_ERROR) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "target creation failed: %s",
                          target ? target->getLastError() : "null target");
      return;
    }
    ++surfaceGeneration;
    action->setRenderTarget(target.get());
    render();
    if (target->getLastSubmissionSerial() != 0) {
      CoinRenderCapabilities caps{};
      if (coin_render_query_capabilities_for_renderer(COIN_RENDER_EXPERIMENTAL_ANDROID_WINDOW,
          COIN_RENDER_RENDERER_VULKAN, &caps, sizeof(caps)) == 0) {
        __android_log_print(ANDROID_LOG_INFO, tag,
          "renderer=%u vendor=%04x device=%04x adapter=%s",
          caps.renderer, caps.vendor_id, caps.device_id, caps.adapter_name);
      }
    }
  }
};

void onCommand(android_app * app, int32_t command) {
  Host & host = *static_cast<Host *>(app->userData);
  switch (command) {
    case APP_CMD_INIT_WINDOW:
      host.createSurface();
      break;
    case APP_CMD_TERM_WINDOW:
      host.releaseSurface();
      break;
    case APP_CMD_RESUME:
      host.resumed = true;
      host.render();
      break;
    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
      host.resumed = false;
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
      host.render();
      break;
    default:
      break;
  }
}
}

extern "C" void android_main(android_app * app) {
  app_dummy();
  SoDB::init();
  CoinRenderAction::initClass();
  Host host;
  host.app = app;
  host.action.reset(new CoinRenderAction);
  host.scene = new SoSeparator;
  host.scene->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0, 0, 4);
  camera->height = 3;
  host.scene->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  host.scene->addChild(model);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(1, 0, 0);
  host.scene->addChild(material);
  host.scene->addChild(new SoCube);
  app->userData = &host;
  app->onAppCmd = onCommand;

  while (!app->destroyRequested) {
    int events = 0;
    android_poll_source * source = nullptr;
    const int timeout = host.resumed && host.target ? 50 : -1;
    const int id = ALooper_pollAll(timeout, nullptr, &events,
                                   reinterpret_cast<void **>(&source));
    if (id >= 0 && source) source->process(app, source);
  }
  host.releaseSurface();
  host.action.reset();
  host.scene->unref();
  __android_log_print(host.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, tag,
                      "P23 smoke finished: %s", host.failed ? "FAILED" : "OK");
}
