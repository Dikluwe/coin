/* P23 NativeActivity smoke: the Android host owns ANativeWindow. Each
 * APP_CMD_INIT_WINDOW creates a new CoinRenderTarget; TERM_WINDOW destroys it
 * before the NDK invalidates the pointer. No parallel scene interpretation. */
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
#include <jni.h>
#include <cstring>

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
bool readLaunchOptions(android_app * app, CoinRenderRenderer & renderer,
                       bool & city, bool & continuous) {
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
    if (getBool) {
      name = env->NewStringUTF("coinrender_city");
      city = env->CallBooleanMethod(intent, getBool, name, city ? JNI_TRUE : JNI_FALSE) == JNI_TRUE;
      env->DeleteLocalRef(name);
      name = env->NewStringUTF("coinrender_continuous");
      continuous = env->CallBooleanMethod(intent, getBool, name, continuous ? JNI_TRUE : JNI_FALSE) == JNI_TRUE;
      env->DeleteLocalRef(name);
    }
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
  // Keep the independent capture target alive across window recreations.
  std::unique_ptr<CoinRenderTarget> captureTarget;
  std::unique_ptr<CoinRenderAction> captureAction;
  uint64_t surfaceGeneration = 0;
  CoinRenderRenderer renderer = COIN_ANDROID_SMOKE_RENDERER;
  SoPerspectiveCamera * cityCamera = nullptr;
  bool city = false;
  bool continuous = false;
  float touchX = 0, touchY = 0;
  bool dragging = false;
  uint64_t frames = 0;
  double renderMillis = 0;
  std::chrono::steady_clock::time_point sampleStart = std::chrono::steady_clock::now();
  bool resumed = false;
  bool failed = false;

  void releaseSurface() {
    if (action) action->setRenderTarget(nullptr);
    target.reset();
  }

  void render(bool qualify = true) {
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
    const auto renderStart = std::chrono::steady_clock::now();
    if (!qualify) {
      action->apply(scene);
      if (action->getLastStatus() != CoinRenderAction::SUCCESS) {
        failed = true;
        resumed = false;
        __android_log_print(ANDROID_LOG_ERROR, tag, "continuous render failed: %s",
                            action->getLastError().getString());
        return;
      }
      const auto now = std::chrono::steady_clock::now();
      renderMillis += std::chrono::duration<double, std::milli>(now - renderStart).count();
      if (++frames % 10 == 0) {
        const double seconds = std::chrono::duration<double>(now - sampleStart).count();
        __android_log_print(ANDROID_LOG_INFO, tag,
          "city=%d generation=%llu frames=%llu serial=%llu fps_cpu_wall=%.2f render_present_mean_ms=%.3f",
          city ? 1 : 0, static_cast<unsigned long long>(surfaceGeneration),
          static_cast<unsigned long long>(frames),
          static_cast<unsigned long long>(target->getLastSubmissionSerial()),
          10.0 / seconds, renderMillis / 10.0);
        renderMillis = 0;
        sampleStart = now;
      }
      return;
    }
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
      if (!captureTarget) {
        CoinRenderOptions options;
        options.renderer = renderer;
        captureTarget.reset(CoinRenderTarget::createOffscreen(captureSize, options));
        captureAction.reset(new CoinRenderAction(SbViewportRegion(64, 64)));
        captureAction->setRenderTarget(captureTarget.get());
      }
      captureAction->apply(scene);
      if (captureAction->getLastStatus() != CoinRenderAction::SUCCESS) {
        failed = true;
        __android_log_print(ANDROID_LOG_ERROR, tag, "offscreen capture failed: %s",
                            captureAction->getLastError().getString());
        return;
      }
      captureTarget->readbackRGBA(rgba);
    }
    if (rgba.size() != size_t(captureSize[0]) * size_t(captureSize[1]) * 4u) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "incomplete RGBA capture");
      return;
    }
    const size_t center = (size_t(captureSize[1] / 2) * size_t(captureSize[0]) + size_t(captureSize[0] / 2)) * 4u;
    size_t colored = 0;
    for (size_t i = 0; i < rgba.size(); i += 4)
      if (rgba[i] > 8 || rgba[i + 1] > 8 || rgba[i + 2] > 8) ++colored;
    if ((city && colored < 32) || (!city &&
        (rgba[center] < rgba[center + 1] + 50 || rgba[center] < rgba[center + 2] + 50))) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "scene capture content check failed");
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
    if (cityCamera) cityCamera->viewAll(scene, SbViewportRegion(size[0], size[1]), 1.15f);
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

int32_t onInput(android_app * app, AInputEvent * event) {
  Host & host = *static_cast<Host *>(app->userData);
  if (!host.cityCamera || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
  const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
  const float x = AMotionEvent_getX(event, 0), y = AMotionEvent_getY(event, 0);
  if (action == AMOTION_EVENT_ACTION_DOWN) {
    host.dragging = true;
    host.touchX = x;
    host.touchY = y;
    return 1;
  }
  if (action == AMOTION_EVENT_ACTION_CANCEL) {
    host.dragging = false;
    return 1;
  }
  // Under a heavy frame the input queue can coalesce MOVE events. Apply the
  // final UP position too, so a short drag cannot disappear between frames.
  if ((action == AMOTION_EVENT_ACTION_MOVE || action == AMOTION_EVENT_ACTION_UP) && host.dragging) {
    SoPerspectiveCamera & camera = *host.cityCamera;
    SbVec3f forward;
    camera.orientation.getValue().multVec(SbVec3f(0, 0, -1), forward);
    const SbVec3f center = camera.position.getValue() + forward * camera.focalDistance.getValue();
    const float width = float(std::max(1, host.target ? host.target->getSize()[0] : 1));
    const float height = float(std::max(1, host.target ? host.target->getSize()[1] : 1));
    SbRotation yaw(SbVec3f(0, 1, 0), -(x - host.touchX) / width * 6.0f);
    SbVec3f offset;
    yaw.multVec(camera.position.getValue() - center, offset);
    // Horizontal drag orbits; vertical drag dollies with a bounded step.
    const float step = (y - host.touchY) / height;
    const float scale = std::exp(step < -.2f ? -.4f : step > .2f ? .4f : step * 2.0f);
    const float focal = camera.focalDistance.getValue();
    const float nextFocal = std::max(5.0f, std::min(5000.0f, focal * scale));
    camera.position = center + offset * (nextFocal / focal);
    camera.focalDistance = nextFocal;
    camera.nearDistance = .1f;
    camera.farDistance = std::max(2000.0f, nextFocal * 4.0f);
    camera.pointAt(center);
    __android_log_print(ANDROID_LOG_INFO, tag, "camera_touch x=%.1f y=%.1f focal=%.3f", x, y,
                        camera.focalDistance.getValue());
  }
  host.touchX = x;
  host.touchY = y;
  if (action == AMOTION_EVENT_ACTION_UP) host.dragging = false;
  return 1;
}

void onCommand(android_app * app, int32_t command) {
  Host & host = *static_cast<Host *>(app->userData);
  __android_log_print(ANDROID_LOG_INFO, tag, "lifecycle command=%d generation=%llu",
                      command, static_cast<unsigned long long>(host.surfaceGeneration));
  switch (command) {
    case APP_CMD_INIT_WINDOW:
      host.createSurface();
      break;
    case APP_CMD_TERM_WINDOW:
      host.releaseSurface();
      break;
    case APP_CMD_RESUME:
      host.sampleStart = std::chrono::steady_clock::now();
      host.renderMillis = 0;
      host.frames = 0;
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
  bool city = true, continuous = true;
  const bool emulatedVulkan = readLaunchOptions(app, requestedRenderer, city, continuous);
  setenv("WGPU_ALLOW_UNDERLYING_NONCOMPLIANT_ADAPTER", emulatedVulkan ? "1" : "0", 1);
  __android_log_print(ANDROID_LOG_INFO, tag,
                      "emulator_noncompliant_vulkan_opt_in=%d", emulatedVulkan ? 1 : 0);

  SoDB::init();
  CoinRenderAction::initClass();
  Host host;
  host.app = app;
  host.renderer = requestedRenderer;
  host.city = city;
  host.continuous = continuous;
  __android_log_print(ANDROID_LOG_INFO, tag, "requested_renderer=%u", unsigned(host.renderer));
  host.action.reset(new CoinRenderAction);
  host.scene = new SoSeparator;
  host.scene->ref();
  if (city) {
    AAsset * asset = AAssetManager_open(app->activity->assetManager, "city-40000.iv", AASSET_MODE_BUFFER);
    if (!asset) {
      __android_log_print(ANDROID_LOG_ERROR, tag, "city asset unavailable");
      host.scene->unref();
      ANativeActivity_finish(app->activity);
      return;
    }
    SoInput input;
    input.setBuffer(AAsset_getBuffer(asset), size_t(AAsset_getLength64(asset)));
    SoSeparator * imported = SoDB::readAll(&input);
    AAsset_close(asset);
    if (!imported) {
      __android_log_print(ANDROID_LOG_ERROR, tag, "city asset parse failed");
      host.scene->unref();
      ANativeActivity_finish(app->activity);
      return;
    }
    host.cityCamera = new SoPerspectiveCamera;
    host.cityCamera->orientation.setValue(SbRotation(SbVec3f(0, 0, -1), SbVec3f(-.5f, -.35f, -1)));
    host.scene->addChild(host.cityCamera);
    host.scene->addChild(imported);
    SoSearchAction count;
    count.setType(SoCube::getClassTypeId());
    count.setInterest(SoSearchAction::ALL);
    count.setSearchingAll(TRUE);
    count.apply(imported);
    const int cubes = count.getPaths().getLength();
    __android_log_print(ANDROID_LOG_INFO, tag,
                        "scene=city-40000 buildings=%d ground=1 triangles=%d continuous=%d",
                        cubes - 1, cubes * 12, continuous ? 1 : 0);
    if (cubes != 40001) {
      __android_log_print(ANDROID_LOG_ERROR, tag, "unexpected city occurrence count=%d", cubes);
      host.scene->unref();
      ANativeActivity_finish(app->activity);
      return;
    }
  } else {
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
  }
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
  app->onInputEvent = onInput;

  while (!app->destroyRequested) {
    int events = 0;
    android_poll_source * source = nullptr;
    const int timeout = host.resumed && host.target ? (host.continuous ? 0 : 50) : -1;
    const int id = ALooper_pollOnce(timeout, nullptr, &events,
                                   reinterpret_cast<void **>(&source));
    if (id >= 0 && source) source->process(app, source);
    if (!app->destroyRequested && !host.failed && host.continuous) host.render(false);
  }
  host.releaseSurface();
  host.action.reset();
  if (host.captureAction) host.captureAction->setRenderTarget(nullptr);
  host.captureAction.reset();
  host.captureTarget.reset();
  host.scene->unref();
  __android_log_print(host.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, tag,
                      "P23 smoke finished: %s", host.failed ? "FAILED" : "OK");
}
