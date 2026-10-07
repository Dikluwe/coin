/* P23 NativeActivity smoke: the Android host owns ANativeWindow. Each
 * APP_CMD_INIT_WINDOW creates a new CoinRenderTarget; TERM_WINDOW destroys it
 * before the NDK invalidates the pointer. No parallel scene interpretation. */
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
#include <jni.h>
#include <cstring>

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
#include <cstdlib>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/system/gl-headers.h>
#include <memory>
#include <vector>

namespace {
const char * const tag = "CoinRenderP23";

// Goldfish reports no Vulkan conformance version. This opt-in is exclusively
// for emulator diagnostics, never enabled for normal APK launches/hardware.
bool readLaunchOptions(android_app * app, CoinRenderRenderer & renderer) {
  char qemu[PROP_VALUE_MAX] = {};
  __system_property_get("ro.kernel.qemu", qemu);
  const bool isEmulator = std::strcmp(qemu, "1") == 0;
  JNIEnv * env = nullptr;
  JavaVM * vm = app->activity->vm;
  const bool attach = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK;
  if (attach && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
  bool allowed = false;
  jclass activity = env->GetObjectClass(app->activity->clazz);
  jmethodID getIntent = env->GetMethodID(activity, "getIntent", "()Landroid/content/Intent;");
  jobject intent = getIntent ? env->CallObjectMethod(app->activity->clazz, getIntent) : nullptr;
  if (intent && !env->ExceptionCheck()) {
    jclass type = env->GetObjectClass(intent);
    jmethodID getInt = env->GetMethodID(type, "getIntExtra", "(Ljava/lang/String;I)I");
    jstring rendererKey = env->NewStringUTF("coinrender_renderer");
    if (getInt) renderer = static_cast<CoinRenderRenderer>(
        env->CallIntMethod(intent, getInt, rendererKey, jint(COIN_ANDROID_SMOKE_RENDERER)));
    env->DeleteLocalRef(rendererKey);

    jmethodID getBool = env->GetMethodID(type, "getBooleanExtra", "(Ljava/lang/String;Z)Z");
    jstring name = env->NewStringUTF("coinrender_allow_noncompliant_vulkan");
    if (getBool) allowed = env->CallBooleanMethod(intent, getBool, name, JNI_FALSE) == JNI_TRUE;
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(type);
  }
  if (env->ExceptionCheck()) { env->ExceptionClear(); allowed = false; }
  if (intent) env->DeleteLocalRef(intent);
  env->DeleteLocalRef(activity);
  if (attach) vm->DetachCurrentThread();
  return allowed && isEmulator && renderer == COIN_RENDER_RENDERER_VULKAN;
}

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
  CoinRenderRenderer renderer = COIN_ANDROID_SMOKE_RENDERER;
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
    const bool windowCapture = renderer != COIN_RENDER_RENDERER_OPENGL &&
                               target->requestWindowReadbackRGBA();
    if (!windowCapture && renderer != COIN_RENDER_RENDERER_OPENGL) {
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
    SbVec2i32 captureSize = size;
    if (windowCapture) {
      target->readbackRGBA(rgba);
    } else {
      // GLES surfaces may lack COPY_SRC. Qualify presentation separately and
      // capture the same scene offscreen; never call this window equivalence.
      captureSize = SbVec2i32(64, 64);
      CoinRenderOptions options;
      options.renderer = renderer;
      std::unique_ptr<CoinRenderTarget> capture(CoinRenderTarget::createOffscreen(captureSize, options));
      CoinRenderAction captureAction(SbViewportRegion(64, 64));
      captureAction.setRenderTarget(capture.get());
      captureAction.apply(scene);
      if (captureAction.getLastStatus() != CoinRenderAction::SUCCESS) {
        failed = true;
        __android_log_print(ANDROID_LOG_ERROR, tag, "offscreen capture failed: %s",
                            captureAction.getLastError().getString());
        return;
      }
      capture->readbackRGBA(rgba);
      captureAction.setRenderTarget(nullptr);
    }
    if (rgba.size() != size_t(captureSize[0]) * size_t(captureSize[1]) * 4u) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "incomplete RGBA capture");
      return;
    }
    const size_t center = (size_t(captureSize[1] / 2) * size_t(captureSize[0]) + size_t(captureSize[0] / 2)) * 4u;
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
      "generation=%llu serial=%llu size=%dx%d rgba_fnv64=%016llx capture_scope=%s",
      static_cast<unsigned long long>(surfaceGeneration),
      static_cast<unsigned long long>(target->getLastSubmissionSerial()),
      size[0], size[1], static_cast<unsigned long long>(checksum(rgba)),
      windowCapture ? "window" : "offscreen");
  }

  void createSurface() {
    releaseSurface();
    if (!app->window) return;
    if (renderer == COIN_RENDER_RENDERER_OPENGL &&
        ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888) != 0) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "native RGBA8 buffer geometry failed");
      return;
    }
    CoinRenderNativeSurfaceDescriptor native{};
    native.abiVersion = COIN_RENDER_NATIVE_SURFACE_ABI_VERSION;
    native.structSize = sizeof(native);
    native.type = COIN_RENDER_SURFACE_ANDROID_NDK;
    native.native.android.nativeWindow = app->window;
    CoinRenderOptions options;
    options.renderer = renderer;
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
          renderer, &caps, sizeof(caps)) == 0) {
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
    case APP_CMD_CONTENT_RECT_CHANGED:
      // GLES configuration overrides buffer dimensions. Reset that override
      // before querying the new window base size after layout/rotation.
      if (host.renderer == COIN_RENDER_RENDERER_OPENGL && app->window &&
          ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888) != 0) {
        host.failed = true;
        __android_log_print(ANDROID_LOG_ERROR, tag, "native resize geometry failed");
        break;
      }
      host.render();
      break;
    default:
      break;
  }
}
}

extern "C" void android_main(android_app * app) {
  setenv("COIN_WGPU_LOG", "debug", 0);
  CoinRenderRenderer requestedRenderer = COIN_ANDROID_SMOKE_RENDERER;
  const bool emulatedVulkan = readLaunchOptions(app, requestedRenderer);
  setenv("WGPU_ALLOW_UNDERLYING_NONCOMPLIANT_ADAPTER", emulatedVulkan ? "1" : "0", 1);
  __android_log_print(ANDROID_LOG_INFO, tag,
                      "emulator_noncompliant_vulkan_opt_in=%d", emulatedVulkan ? 1 : 0);

  SoDB::init();
  CoinRenderAction::initClass();
  Host host;
  host.app = app;
  host.renderer = requestedRenderer;
  __android_log_print(ANDROID_LOG_INFO, tag, "requested_renderer=%u", unsigned(host.renderer));
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
#if COIN_ANDROID_CPU_ONLY
  {
    SoGLRenderAction legacy(SbViewportRegion(32, 32));
    legacy.apply(host.scene);
    SoOffscreenRenderer offscreen(SbViewportRegion(32, 32));
    const bool rejected = legacy.hasTerminated() && !offscreen.render(host.scene);
    host.failed = !rejected;
    __android_log_print(rejected ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, tag,
                        "legacy_gl_unavailable=%d", rejected ? 1 : 0);
  }
#endif

  app->userData = &host;
  app->onAppCmd = onCommand;

  while (!app->destroyRequested) {
    int events = 0;
    android_poll_source * source = nullptr;
    const int timeout = host.resumed && host.target ? 50 : -1;
    const int id = ALooper_pollOnce(timeout, nullptr, &events,
                                   reinterpret_cast<void **>(&source));
    if (id >= 0 && source) source->process(app, source);
  }
  host.releaseSurface();
  host.action.reset();
  host.scene->unref();
  __android_log_print(host.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, tag,
                      "P23 smoke finished: %s", host.failed ? "FAILED" : "OK");
}
