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
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderNativeSurface.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/rendering/CoinRenderRetainedRenderer.h>
#include <Inventor/nodes/SoDepthBuffer.h>
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
#include <cstdio>
#include <string>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/system/gl-headers.h>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace {
const char * const tag = "CoinRenderP23";

// Goldfish reports no Vulkan conformance version. This opt-in is exclusively
// for emulator diagnostics, never enabled for normal APK launches/hardware.
bool readLaunchOptions(android_app * app, CoinRenderRenderer & renderer,
                       bool & city, bool & continuous, bool & trace, bool & million,
                       uint32_t & samples, bool & retained, bool & comparison) {
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
    jstring samplesKey = env->NewStringUTF("coinrender_samples");
    if (getInt) samples = static_cast<uint32_t>(
        env->CallIntMethod(intent, getInt, samplesKey, jint(1)));
    env->DeleteLocalRef(samplesKey);

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
      name = env->NewStringUTF("coinrender_trace");
      trace = env->CallBooleanMethod(intent, getBool, name, JNI_FALSE) == JNI_TRUE;
      env->DeleteLocalRef(name);
      name = env->NewStringUTF("coinrender_million_city");
      million = env->CallBooleanMethod(intent, getBool, name, million ? JNI_TRUE : JNI_FALSE) == JNI_TRUE;
      env->DeleteLocalRef(name);
      name = env->NewStringUTF("coinrender_retained");
      retained = env->CallBooleanMethod(intent, getBool, name, JNI_FALSE) == JNI_TRUE;
      env->DeleteLocalRef(name);
      name = env->NewStringUTF("coinrender_compare");
      comparison = env->CallBooleanMethod(intent, getBool, name, JNI_FALSE) == JNI_TRUE;
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

// Android's input/lifecycle looper must acknowledge events even while a large
// scene is parsing or capturing. All Coin and GPU work stays on one worker;
// the looper passes only copied motion values and acquired window references.
struct HostEvent {
  int command = 0;
  int action = 0;
  float x = 0, y = 0;
  bool motion = false;
  ANativeWindow * window = nullptr;
};
struct HostEvents {
  std::mutex mutex;
  std::condition_variable ready;
  std::deque<HostEvent> pending;
  bool stop = false, done = false;
  ~HostEvents() {
    for (const auto & event : pending) if (event.window) ANativeWindow_release(event.window);
  }
  void push(const HostEvent & event) {
    std::lock_guard<std::mutex> lock(mutex);
    // Consecutive moves may collapse; DOWN/UP and lifecycle boundaries remain.
    if (event.motion && event.action == AMOTION_EVENT_ACTION_MOVE && !pending.empty() &&
        pending.back().motion && pending.back().action == AMOTION_EVENT_ACTION_MOVE)
      pending.back() = event;
    else pending.push_back(event);
    ready.notify_one();
  }
};

struct Host {
  android_app * app = nullptr;
  SoSeparator * scene = nullptr;
  std::unique_ptr<CoinRenderAction> action;
  std::unique_ptr<CoinRenderRetainedRenderer> retained;
  bool retainedPath = false, comparison = false, comparisonReady = false;
  std::vector<double> comparisonTimes;
  unsigned comparisonWarmup = 0;
  const char * pathName() const { return retainedPath ? "retained" : "current"; }
  CoinRenderAction::Status selectedRender(CoinRenderTarget & output, const SbViewportRegion & viewport) {
    if (retainedPath) {
      if (!retained) retained.reset(new CoinRenderRetainedRenderer(viewport));
      retained->setViewportRegion(viewport);
      return retained->render(scene, output);
    }
    if (!action) action.reset(new CoinRenderAction(viewport));
    // These setters invalidate CoinRenderAction's capture even for identical
    // values. Preserve its ordinary hot-cache path in the comparison.
    if (!(action->getViewportRegion() == viewport)) action->setViewportRegion(viewport);
    if (action->getRenderTarget() != &output) action->setRenderTarget(&output);
    action->apply(scene);
    return action->getLastStatus();
  }
  void logRetained() {
    if (!retained) return;
    const auto stats = retained->getStatistics();
    __android_log_print(ANDROID_LOG_INFO, tag,
      "retained_stats captures=%llu plans=%llu transport_builds=%llu retained_frames=%llu commands=%llu resources=%llu vertices=%llu cameras=%llu materials=%llu command_storage_bytes=%llu path_storage_bytes=%llu dependency_ranges=%llu compaction_passes=%llu schedule_bytes=%llu",
      (unsigned long long)stats.captures, (unsigned long long)stats.planBuilds,
      (unsigned long long)stats.transportBuilds,(unsigned long long)stats.retainedFrames,
      (unsigned long long)stats.nativeCommands,(unsigned long long)stats.nativeGeometryResources,
      (unsigned long long)stats.transportVertices,(unsigned long long)stats.transportCameras,(unsigned long long)stats.materialSlots,(unsigned long long)stats.nativeCommandStorageBytes,(unsigned long long)stats.nativePathStorageBytes,(unsigned long long)stats.nativeDependencyRanges,(unsigned long long)stats.nativeCompactionPasses,(unsigned long long)stats.transportScheduleBytes);
  }
  bool writeComparison(const char * suffix, const void * data, size_t bytes) {
    const std::string name = std::string(app->activity->internalDataPath) + "/million-" + pathName() + suffix;
    FILE * file = std::fopen(name.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && ok;
  }
  void renderComparison(bool qualify, const SbVec2i32 & size) {
    const SbViewportRegion viewport(size[0],size[1]);
    const auto start = std::chrono::steady_clock::now();
    const auto status = selectedRender(*target, viewport);
    const double milliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if (status != CoinRenderAction::SUCCESS) {
      failed = true; resumed = false;
      __android_log_print(ANDROID_LOG_ERROR, tag,"comparison_failed path=%s status=%d reason=%s",pathName(),int(status),
        retainedPath ? retained->getLastError().getString() : action->getLastError().getString());
      return;
    }
    if (qualify || !comparisonReady) {
      // Same viewport/projection for both paths and both targets. The retained
      // action/list/plan is reused for window -> offscreen -> window; only the
      // owned executor transport changes target. No second capture/planner.
      __android_log_print(ANDROID_LOG_INFO,tag,"comparison_first path=%s render_ms=%.3f size=%dx%d camera=%.9g,%.9g,%.9g focal=%.9g near=%.9g far=%.9g",
        pathName(),milliseconds,size[0],size[1],cityCamera?cityCamera->position.getValue()[0]:0,
        cityCamera?cityCamera->position.getValue()[1]:0,cityCamera?cityCamera->position.getValue()[2]:0,
        cityCamera?cityCamera->focalDistance.getValue():0,cityCamera?cityCamera->nearDistance.getValue():0,cityCamera?cityCamera->farDistance.getValue():0);
      CoinRenderOptions options;options.renderer=renderer;options.sampleCount=samples;
      captureTarget.reset(CoinRenderTarget::createOffscreen(size,options));
      const auto captured = selectedRender(*captureTarget,viewport);
      if (captured != CoinRenderAction::SUCCESS) { failed=true;resumed=false;__android_log_print(ANDROID_LOG_ERROR,tag,"comparison_capture_failed path=%s reason=%s",pathName(),retainedPath?retained->getLastError().getString():action->getLastError().getString());return; }
      std::vector<uint8_t> image;std::vector<float> depth;
      captureTarget->readbackRGBA(image);captureTarget->readbackDepth(depth);
      if (image.size()!=size_t(size[0])*size[1]*4 || depth.size()!=size_t(size[0])*size[1] ||
          !writeComparison(".rgba",image.data(),image.size()) || !writeComparison(".depth",depth.data(),depth.size()*sizeof(float))) {
        failed=true;resumed=false;return;
      }
      __android_log_print(ANDROID_LOG_INFO,tag,"comparison_capture path=%s rgba_fnv64=%016llx pixels=%zu",pathName(),(unsigned long long)checksum(image),image.size()/4);
      if (selectedRender(*target,viewport)!=CoinRenderAction::SUCCESS) { failed=true;resumed=false;return; }
      captureTarget.reset();comparisonReady=true;comparisonTimes.clear();comparisonWarmup=0;
      logRetained();
      return;
    }
    if (++comparisonWarmup <= 10) return;
    comparisonTimes.push_back(milliseconds);
    if (comparisonTimes.size()==120) {
      auto sorted=comparisonTimes;std::sort(sorted.begin(),sorted.end());
      const double median=(sorted[59]+sorted[60])*.5;
      __android_log_print(ANDROID_LOG_INFO,tag,"comparison_perf path=%s samples=120 median_ms=%.6f p90_ms=%.6f serial=%llu",
        pathName(),median,sorted[107],(unsigned long long)target->getLastSubmissionSerial());
      writeComparison(".times",comparisonTimes.data(),comparisonTimes.size()*sizeof(double));
      logRetained();
    }
    if (comparisonTimes.size()>120) comparisonTimes.pop_back();
  }
  std::unique_ptr<CoinRenderTarget> target;
  // Keep the independent capture target alive across window recreations.
  std::unique_ptr<CoinRenderTarget> captureTarget;
  std::unique_ptr<CoinRenderAction> captureAction;
  uint64_t surfaceGeneration = 0;
  CoinRenderRenderer renderer = COIN_ANDROID_SMOKE_RENDERER;
  uint32_t samples = 1;
  SoPerspectiveCamera * cityCamera = nullptr;
  bool cityCameraFramed = false;
  bool city = false;
  bool million = false;
  bool selectionSaved = false;
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
    retained.reset(); comparisonReady=false;
    target.reset();
    if (million && !retainedPath) action.reset(new CoinRenderAction);
  }

  void render(bool qualify = true) {
    if (!resumed || !target || !app->window) return;
    const SbVec2i32 size(ANativeWindow_getWidth(app->window),
                         ANativeWindow_getHeight(app->window));
    if (size[0] <= 0 || size[1] <= 0) {
      target->resize(SbVec2i32(0, 0));
      return;
    }
    const bool resized = size != target->getSize();
    if (resized && !target->resize(size)) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, tag, "resize failed: %s", target->getLastError());
      return;
    }
    // Android can publish the new native size after its layout callback.
    // If a continuous frame observes it first, qualify the new dimensions
    // there too; the million-city callback deliberately skips unchanged sizes.
    qualify = qualify || resized;
    const SbViewportRegion viewport(size[0], size[1]);
    if (comparison || retainedPath) { renderComparison(qualify || resized,size); return; }
    if (million && qualify) {
      // Requalification cannot reuse the old plan. Drop it before recapture
      // so resize/resume also avoid retaining two multi-GB CPU plans.
      action->setRenderTarget(nullptr);
      action.reset(new CoinRenderAction(viewport));
      action->setRenderTarget(target.get());
    } else if (!(action->getViewportRegion() == viewport)) action->setViewportRegion(viewport);
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
      if (million) {
        // Keep only one large CPU plan alive while qualifying both targets.
        // The persistent offscreen target still guards the runtime lifetime.
        action->setRenderTarget(nullptr);
        action.reset();
      }
      if (!captureTarget) {
        CoinRenderOptions options;
        options.renderer = renderer;
        options.sampleCount = samples;
        captureTarget.reset(CoinRenderTarget::createOffscreen(captureSize, options));
      }
      if (!captureAction) {
        captureAction.reset(new CoinRenderAction(SbViewportRegion(64, 64)));
        captureAction->setRenderTarget(captureTarget.get());
      }
      captureAction->apply(scene);
      if (million) {
        // Restore a usable window action even if offscreen submission failed.
        // This empty action holds no captured plan until the normal apply.
        action.reset(new CoinRenderAction(viewport));
        action->setRenderTarget(target.get());
      }
      if (captureAction->getLastStatus() != CoinRenderAction::SUCCESS) {
        failed = true;
        __android_log_print(ANDROID_LOG_ERROR, tag, "offscreen capture failed: %s",
                            captureAction->getLastError().getString());
        return;
      }
      captureTarget->readbackRGBA(rgba);
      if (million) {
        captureAction->setRenderTarget(nullptr);
        captureAction.reset();
      }
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
      __android_log_print(ANDROID_LOG_ERROR, tag,
          "scene capture content check failed: samples=%u center=%u,%u,%u,%u colored=%zu",
          samples, unsigned(rgba[center]), unsigned(rgba[center + 1]),
          unsigned(rgba[center + 2]), unsigned(rgba[center + 3]), colored);
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
    options.sampleCount = samples;
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
    if (action) action->setRenderTarget(target.get());
    if (cityCamera) {
      // The camera belongs to this Activity, not to an ANativeWindow. A
      // TERM_WINDOW/INIT_WINDOW pair must keep the user's orbit and zoom.
      if (!cityCameraFramed) {
        cityCamera->viewAll(scene, SbViewportRegion(size[0], size[1]), 1.15f);
        cityCameraFramed = true;
      }
      __android_log_print(ANDROID_LOG_INFO, tag,
        "camera_surface generation=%llu focal=%.3f",
        static_cast<unsigned long long>(surfaceGeneration),
        cityCamera->focalDistance.getValue());
    }
    render();
    if (target->getLastSubmissionSerial() != 0) {
      if (city && !failed && !selectionSaved) {
        const std::string path = std::string(app->activity->internalDataPath) + "/coinrender-city-selection.txt";
        if (FILE * file = std::fopen(path.c_str(), "w")) {
          std::fprintf(file, "%d\n", million ? 1000 : 200);
          std::fclose(file);
          selectionSaved = true;
        }
      }
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

int32_t onMotion(android_app * app, int action, float x, float y) {
  Host & host = *static_cast<Host *>(app->userData);
  if (!host.cityCamera) return 0;
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
    const float nextFocal = std::max(5.0f, std::min(host.million ? 50000.0f : 5000.0f, focal * scale));
    camera.position = center + offset * (nextFocal / focal);
    camera.focalDistance = nextFocal;
    camera.nearDistance = .1f;
    camera.farDistance = std::max(host.million ? 20000.0f : 2000.0f, nextFocal * 4.0f);
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
      // A layout can repeat unchanged dimensions. Recapture the million-city
      // diagnostic once per actual resize, preserving normal cached frames.
      if (!host.million || !host.target || !app->window ||
          host.target->getSize() != SbVec2i32(ANativeWindow_getWidth(app->window),
                                            ANativeWindow_getHeight(app->window)))
        host.render();
      break;
    default:
      break;
  }
}
}

static void runRenderer(android_app * app, HostEvents & events) {
  setenv("COIN_WGPU_LOG", "debug", 0);
  CoinRenderRenderer requestedRenderer = COIN_ANDROID_SMOKE_RENDERER;
  bool city = true, continuous = true, trace = false, million = false, retainedPath = false, comparison = false;
  uint32_t samples = 1;
  const std::string selectionPath = std::string(app->activity->internalDataPath) + "/coinrender-city-selection.txt";
  if (FILE * file = std::fopen(selectionPath.c_str(), "r")) {
    int grid = 0;
    if (std::fscanf(file, "%d", &grid) == 1) million = grid == 1000;
    std::fclose(file);
  }
  const bool emulatedVulkan = readLaunchOptions(app, requestedRenderer, city, continuous, trace, million, samples, retainedPath, comparison);
  if (samples != 1 && samples != 2 && samples != 4) {
    __android_log_print(ANDROID_LOG_ERROR, tag, "unsupported sample count=%u", samples);
    ANativeActivity_finish(app->activity);
    return;
  }
  if (trace) {
    const std::string file = std::string(app->activity->internalDataPath) + "/coinrender-phases.log";
    if (!std::freopen(file.c_str(), "w", stderr)) {
      ANativeActivity_finish(app->activity);
      return;
    }
    setenv("COIN_RENDER_TRACE_PHASES", "1", 1);
  } else unsetenv("COIN_RENDER_TRACE_PHASES");
  setenv("WGPU_ALLOW_UNDERLYING_NONCOMPLIANT_ADAPTER", emulatedVulkan ? "1" : "0", 1);
  __android_log_print(ANDROID_LOG_INFO, tag,
                      "emulator_noncompliant_vulkan_opt_in=%d", emulatedVulkan ? 1 : 0);

  SoDB::init();
  CoinRenderAction::initClass();
  Host host;
  host.app = app;
  host.renderer = requestedRenderer;
  host.samples = samples;
  host.city = city;
  host.million = million;
  host.continuous = continuous; host.retainedPath=retainedPath; host.comparison=comparison || retainedPath;
  __android_log_print(ANDROID_LOG_INFO,tag,"render_path=%s comparison=%d",host.pathName(),host.comparison?1:0);
  __android_log_print(ANDROID_LOG_INFO, tag, "requested_renderer=%u", unsigned(host.renderer));
  __android_log_print(ANDROID_LOG_INFO, tag, "requested_samples=%u", samples);
  if (!retainedPath) host.action.reset(new CoinRenderAction);
  host.scene = new SoSeparator;
  host.scene->ref();
  if (host.comparison) host.scene->addChild(new SoDepthBuffer); // Same explicit LESS state for both paths.
  if (city) {
    AAsset * asset = AAssetManager_open(app->activity->assetManager, million ? "city-1000000.iv" : "city-40000.iv", AASSET_MODE_BUFFER);
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
    // Count occurrences without retaining a million paths to the shared cube.
    // Detaching those path auditors otherwise makes startup quadratic.
    int cubes = 0;
    SoCallbackAction count;
    count.addPreCallback(SoCube::getClassTypeId(),
      [](void * data, SoCallbackAction *, const SoNode *) {
        ++*static_cast<int *>(data);
        return SoCallbackAction::CONTINUE;
      }, &cubes);
    count.apply(imported);
    __android_log_print(ANDROID_LOG_INFO, tag,
                        "scene=city-%d buildings=%d ground=1 triangles=%d continuous=%d",
                        million ? 1000000 : 40000, cubes - 1, cubes * 12, continuous ? 1 : 0);
    if (cubes != (million ? 1000001 : 40001)) {
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
  while (true) {
    std::deque<HostEvent> batch;
    {
      std::unique_lock<std::mutex> lock(events.mutex);
      if (events.pending.empty() && !events.stop && !(host.resumed && host.target && host.continuous))
        events.ready.wait(lock, [&] { return events.stop || !events.pending.empty(); });
      if (events.stop) break;
      batch.swap(events.pending);
    }
    for (auto & event : batch) {
      if (event.motion) onMotion(app, event.action, event.x, event.y);
      else {
        if (event.command == APP_CMD_INIT_WINDOW) {
          host.releaseSurface();
          if (app->window) ANativeWindow_release(app->window);
          app->window = event.window; event.window = nullptr;
        }
        onCommand(app, event.command);
        if (event.command == APP_CMD_TERM_WINDOW && app->window) {
          ANativeWindow_release(app->window); app->window = nullptr;
        }
      }
    }
    if (!host.failed && host.continuous) host.render(false);
  }
  host.releaseSurface();
  host.action.reset();
  if (host.captureAction) host.captureAction->setRenderTarget(nullptr);
  host.captureAction.reset();
  host.captureTarget.reset();
  host.scene->unref();
  if (app->window) { ANativeWindow_release(app->window); app->window = nullptr; }
  __android_log_print(host.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, tag,
                      "P23 smoke finished: %s", host.failed ? "FAILED" : "OK");
}

extern "C" void android_main(android_app * app) {
  HostEvents events;
  app->userData = &events;
  app->onAppCmd = [](android_app * source, int32_t command) {
    auto & queue = *static_cast<HostEvents *>(source->userData);
    HostEvent event; event.command = command;
    if (command == APP_CMD_INIT_WINDOW && source->window) {
      event.window = source->window; ANativeWindow_acquire(event.window);
    }
    queue.push(event);
  };
  app->onInputEvent = [](android_app * source, AInputEvent * input) -> int32_t {
    if (AInputEvent_getType(input) != AINPUT_EVENT_TYPE_MOTION) return 0;
    HostEvent event; event.motion = true;
    event.action = AMotionEvent_getAction(input) & AMOTION_EVENT_ACTION_MASK;
    event.x = AMotionEvent_getX(input, 0); event.y = AMotionEvent_getY(input, 0);
    static_cast<HostEvents *>(source->userData)->push(event);
    return 1;
  };
  std::thread renderer([&] {
    // A private view avoids racing android_app.window/userData. NativeActivity
    // remains alive until android_main joins this worker during destruction.
    android_app view{}; view.activity = app->activity;
    runRenderer(&view, events);
    { std::lock_guard<std::mutex> lock(events.mutex); events.done = true; }
    ALooper_wake(app->looper);
  });
  while (!app->destroyRequested) {
    int flags = 0;
    android_poll_source * source = nullptr;
    const int id = ALooper_pollOnce(-1, nullptr, &flags, reinterpret_cast<void **>(&source));
    if (id >= 0 && source) source->process(app, source);
    bool done;
    { std::lock_guard<std::mutex> lock(events.mutex); done = events.done; }
    if (done && !app->destroyRequested) ANativeActivity_finish(app->activity);
  }
  { std::lock_guard<std::mutex> lock(events.mutex); events.stop = true; }
  events.ready.notify_one();
  renderer.join();
}
