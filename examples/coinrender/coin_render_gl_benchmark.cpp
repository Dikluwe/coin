// Opt-in end-to-end offscreen comparison. Both paths include RGBA readback;
// these numbers are observations on one adapter/driver, never a speed SLA.

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbRotation.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>

#include "CoinRenderBenchmarkAnimation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <cerrno>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

uint64_t rgbaChecksum(const uint8_t * pixels, size_t bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < bytes; ++i) {
    hash ^= pixels[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

void report(const char * backend, const std::vector<double> & values) {
  std::vector<double> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const size_t median = sorted.size() / 2;
  const size_t p95 = (sorted.size() * 95 + 99) / 100 - 1;
  std::cout << backend << " frames=" << sorted.size()
            << " median_ms=" << sorted[median]
            << " p95_ms=" << sorted[p95]
            << " min_ms=" << sorted.front()
            << " max_ms=" << sorted.back() << '\n';
}

struct FrameSample {
  std::string backend;
  int index;
  int64_t logicalFrame;
  bool warmup;
  double updateMs, renderMs, publicationMs, totalMs;
};

void reportTiming(const std::string & label, const std::vector<double> & values) {
  std::vector<double> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const auto percentile = [&](size_t percent) {
    return sorted[(sorted.size() * percent + 99) / 100 - 1];
  };
  const std::streamsize precision = std::cout.precision();
  std::cout << std::setprecision(9) << label << " frames=" << sorted.size()
            << " median_ms=" << (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) * 0.5
            << " p95_ms=" << percentile(95)
            << " p99_ms=" << percentile(99)
            << " max_ms=" << sorted.back()
            << " over_16_67=" << std::count_if(values.begin(), values.end(),
                 [](double value) { return value > 1000.0 / 60.0; })
            << " over_33_33=" << std::count_if(values.begin(), values.end(),
                 [](double value) { return value > 1000.0 / 30.0; }) << '\n';
  std::cout.precision(precision);
}

bool parseInteger(const std::string & text, long & value) {
  if (text.empty()) return false;
  char * end = NULL;
  errno = 0;
  value = std::strtol(text.c_str(), &end, 10);
  return errno == 0 && end == text.c_str() + text.size();
}

bool parseCaptureFrames(const std::string & text, std::vector<int> & indices) {
  if (text.empty() || text.back() == ',') return false;
  std::istringstream input(text);
  std::string field;
  while (std::getline(input, field, ',')) {
    long index = 0;
    if (!parseInteger(field, index) || index < 0 || index > 9999) return false;
    indices.push_back(static_cast<int>(index));
  }
  std::sort(indices.begin(), indices.end());
  return !indices.empty() &&
    std::adjacent_find(indices.begin(), indices.end()) == indices.end();
}

bool writeRgbImage(const std::string & path, const uint8_t * rgba,
                   int side, bool bottomUp, uint64_t * rgbHash = NULL) {
  std::ofstream image(path.c_str(), std::ios::binary);
  image << "P6\n" << side << ' ' << side << "\n255\n";
  std::vector<uint8_t> row(size_t(side) * 3u);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (int y = 0; y < side; ++y) {
    const int sourceRow = bottomUp ? side - 1 - y : y;
    for (int x = 0; x < side; ++x)
      std::memcpy(row.data() + size_t(x) * 3u,
                  rgba + (size_t(sourceRow) * side + x) * 4u, 3u);
    image.write(reinterpret_cast<const char *>(row.data()), row.size());
    if (rgbHash) for (uint8_t channel : row) {
      hash ^= channel;
      hash *= UINT64_C(1099511628211);
    }
  }
  image.close();
  if (rgbHash) *rgbHash = hash;
  return bool(image);
}

SoSeparator * createScene(SoPerspectiveCamera ** cameraOut) {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  *cameraOut = camera;
  camera->position.setValue(0.0f, 0.0f, 17.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 40.0f;
  root->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.7f, 0.35f, 0.2f);
  root->addChild(material);
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 6; ++x) {
      SoSeparator * item = new SoSeparator;
      SoTranslation * translation = new SoTranslation;
      translation->translation.setValue((x - 2.5f) * 1.5f,
                                        (y - 2.5f) * 1.5f, 0.0f);
      item->addChild(translation);
      SoCube * cube = new SoCube;
      cube->width = 0.9f;
      cube->height = 0.9f;
      cube->depth = 0.9f;
      item->addChild(cube);
      root->addChild(item);
    }
  }
  return root;
}

SoSeparator * loadScene(const std::string & path, int side,
                        SoPerspectiveCamera ** cameraOut) {
  SoInput input;
  const auto parseBegin = Clock::now();
  if (!input.openFile(path.c_str())) return NULL;
  SoSeparator * imported = SoDB::readAll(&input);
  if (!imported || imported->getNumChildren() == 0) return NULL;
  const auto parsed = Clock::now();

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  *cameraOut = camera;
  camera->orientation.setValue(SbRotation(SbVec3f(0.0f, 0.0f, -1.0f),
                                          SbVec3f(-0.5f, -0.35f, -1.0f)));
  root->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = SoLightModel::BASE_COLOR;
  root->addChild(model);
  root->addChild(imported);
  const auto fitBegin = Clock::now();
  camera->viewAll(root, SbViewportRegion(side, side), 1.15f);
  const auto fitted = Clock::now();
  std::cout << "scene_detail parse_ms=" << elapsedMs(parseBegin, parsed)
            << " wrapper_ms=" << elapsedMs(parsed, fitBegin)
            << " camera_fit_ms=" << elapsedMs(fitBegin, fitted) << '\n';
  return root;
}
}

int main(int argc, char ** argv) {
  const auto mainBegin = Clock::now();
  int frames = 30;
  int warmup = 8;
  int side = 256;
  int asyncDepth = 0;
  bool dynamic = false;
  bool materialDynamic = false;
  std::string backend = "both";
  std::string readback = "color";
  std::string transparency = "object";
  std::string rgbaOutput = "copy";
  std::string scenePath;
  std::string imageOutput;
  std::string animationName = "static";
  CoinRenderBenchmarkAnimation::Mode animationMode = CoinRenderBenchmarkAnimation::Mode::STATIC;
  bool animationSpecified = false, percentSpecified = false, stepSpecified = false;
  int animatedPercent = 100;
  int animationStep = 1;
  std::string samplesOutput, captureFrameText, capturePrefix;
  bool samplesSpecified = false, captureFramesSpecified = false, capturePrefixSpecified = false;
  std::vector<int> captureFrames;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) side = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--async-depth") == 0 && i + 1 < argc) asyncDepth = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--image-output") == 0 && i + 1 < argc) imageOutput = argv[++i];
    else if (std::strcmp(argv[i], "--animation") == 0 && i + 1 < argc) {
      animationName = argv[++i]; animationSpecified = true;
    }
    else if ((std::strcmp(argv[i], "--animated-percent") == 0 ||
              std::strcmp(argv[i], "--animation-step") == 0) && i + 1 < argc) {
      const bool percent = std::strcmp(argv[i], "--animated-percent") == 0;
      long value = 0;
      if (!parseInteger(argv[++i], value) || value < 1 ||
          value > (percent ? 100 : std::numeric_limits<int>::max())) {
        std::cerr << "Invalid animation percentage or step\n"; return 2;
      }
      if (percent) { animatedPercent = static_cast<int>(value); percentSpecified = true; }
      else { animationStep = static_cast<int>(value); stepSpecified = true; }
    }
    else if (std::strcmp(argv[i], "--samples-output") == 0 && i + 1 < argc) {
      samplesOutput = argv[++i]; samplesSpecified = true;
    }
    else if (std::strcmp(argv[i], "--capture-frames") == 0 && i + 1 < argc) {
      captureFrameText = argv[++i]; captureFramesSpecified = true;
    }
    else if (std::strcmp(argv[i], "--capture-prefix") == 0 && i + 1 < argc) {
      capturePrefix = argv[++i]; capturePrefixSpecified = true;
    }
    else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) scenePath = argv[++i];
    else if (std::strcmp(argv[i], "--dynamic") == 0) dynamic = true;
    else if (std::strcmp(argv[i], "--material-dynamic") == 0) materialDynamic = true;
    else if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) backend = argv[++i];
    else if (std::strcmp(argv[i], "--readback") == 0 && i + 1 < argc) readback = argv[++i];
    else if (std::strcmp(argv[i], "--transparency") == 0 && i + 1 < argc) transparency = argv[++i];
    else if (std::strcmp(argv[i], "--rgba-output") == 0 && i + 1 < argc) rgbaOutput = argv[++i];
    else {
      std::cerr << "Usage: coin_render_gl_benchmark [--frames 30] [--warmup 8] [--image-output frame.ppm]"
                   " [--size 256] [--scene normalized.iv] [--dynamic|--material-dynamic]"
                   " [--animation static|camera|transforms|materials|geometry] [--animated-percent 1..100]"
                   " [--animation-step 1] [--samples-output samples.csv]"
                   " [--capture-frames 0,60,120] [--capture-prefix path]"
                   " [--backend both|wgpu|bgfx|gl] [--transparency object|weighted_oit|sorted_layers] [--readback color|color-depth] [--rgba-output copy|borrow] [--async-depth 2|3]\n";
      return 2;
    }
  }
  const bool animationEnabled = animationSpecified || percentSpecified || stepSpecified ||
    samplesSpecified || captureFramesSpecified;
  if (!CoinRenderBenchmarkAnimation::parseMode(animationName, animationMode) ||
      (captureFramesSpecified && !parseCaptureFrames(captureFrameText, captureFrames)) ||
      (samplesSpecified && samplesOutput.empty()) ||
      (capturePrefixSpecified && capturePrefix.empty()) ||
      ((!captureFrames.empty()) != (!capturePrefix.empty())) ||
      (!captureFrames.empty() && captureFrames.back() >= frames) ||
      (animationEnabled && (dynamic || materialDynamic)) ||
      (asyncDepth != 0 && (animationSpecified || percentSpecified || stepSpecified ||
                          samplesSpecified || captureFramesSpecified || capturePrefixSpecified)) ||
      (!captureFrames.empty() && rgbaOutput != "copy")) {
    std::cerr << "Invalid animation/capture options or incompatible legacy/async mode\n";
    return 2;
  }
  if (frames < 1 || frames > 10000 || warmup < 0 || warmup > 10000 ||
      side < 1 || side > 2048 || (dynamic && materialDynamic) ||
      (backend != "both" && backend != "wgpu" && backend != "bgfx" && backend != "gl") ||
      (readback != "color" && readback != "color-depth") ||
      (transparency != "object" && transparency != "weighted_oit" &&
       transparency != "sorted_layers") ||
      ((backend == "gl" || backend == "wgpu" || backend == "both") &&
       transparency == "weighted_oit") ||
      (rgbaOutput != "copy" && rgbaOutput != "borrow") ||
      (asyncDepth != 0 && asyncDepth != 2 && asyncDepth != 3) ||
      (asyncDepth == 3 && backend != "bgfx") ||
      (asyncDepth != 0 && (backend != "wgpu" && backend != "bgfx")) ||
      (asyncDepth != 0 && rgbaOutput != "copy") ||
      (!imageOutput.empty() && (asyncDepth != 0 || rgbaOutput != "copy"))) {
    std::cerr << "Invalid benchmark dimensions or sample count\n";
    return 2;
  }
  if (readback == "color-depth" && backend != "wgpu") {
    std::cerr << "Color+depth readback is WebGPU-only; GL comparison reads color\n";
    return 2;
  }

#ifdef _WIN32
  _putenv_s("COIN_RENDER_TRANSPARENCY", transparency.c_str());
#else
  setenv("COIN_RENDER_TRANSPARENCY", transparency.c_str(), 1);
#endif
  const auto initBegin = Clock::now();
  SoDB::init();
  CoinRenderAction::initClass();
  const auto initialized = Clock::now();
  CoinRenderCapabilities caps{};
  const bool runWgpu = backend != "gl";
  const bool runGl = backend == "both" || backend == "gl";
  const char * rendererLabel = "WebGPU";
  bool useBgfx = false;
  if (runWgpu) {
    if (coin_render_query_capabilities(
          COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0) {
      std::cerr << "Experimental offscreen capabilities unavailable\n";
      return 2;
    }
    const bool rust = caps.backend == COIN_RENDER_EXPERIMENTAL_RUST && caps.gpu_available;
    const bool bgfx = caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION && caps.gpu_available;
    useBgfx = bgfx;
    if ((backend == "wgpu" && !rust) || (backend == "bgfx" && !bgfx) ||
        (backend == "both" && !rust && !bgfx)) {
      std::cerr << "Requested GPU backend unavailable\n";
      return 2;
    }
    if (bgfx) {
      switch (caps.renderer) {
      case COIN_RENDER_RENDERER_D3D12: rendererLabel = "BGFX-D3D12"; break;
      case COIN_RENDER_RENDERER_OPENGL: rendererLabel = "BGFX-OpenGL"; break;
      case COIN_RENDER_RENDERER_VULKAN: rendererLabel = "BGFX-Vulkan"; break;
      default: rendererLabel = "BGFX"; break;
      }
    }
    if (bgfx && readback != "color") {
      std::cerr << "BGFX evaluation supports RGBA readback only\n";
      return 2;
    }
    if (bgfx && asyncDepth != 0) {
      char depthText[2] = {static_cast<char>('0' + asyncDepth), '\0'};
#ifdef _WIN32
      _putenv_s("COIN_BGFX_READBACK_PIPELINE_DEPTH", depthText);
#else
      setenv("COIN_BGFX_READBACK_PIPELINE_DEPTH", depthText, 1);
#endif
    }
  }

  const auto probed = Clock::now();
  std::cout << "startup_detail arguments_ms=" << elapsedMs(mainBegin, initBegin)
            << " coin_init_ms=" << elapsedMs(initBegin, initialized)
            << " capability_probe_ms=" << elapsedMs(initialized, probed) << '\n';
  SoPerspectiveCamera * camera = NULL;
  const Clock::time_point loadBegin = Clock::now();
  SoSeparator * root = scenePath.empty() ? createScene(&camera) :
    loadScene(scenePath, side, &camera);
  if (!root) {
    std::cerr << "Cannot read normalized Inventor scene\n";
    return 2;
  }
  std::cout << "scene_load_ms=" << std::chrono::duration<double, std::milli>(
    Clock::now() - loadBegin).count() << std::endl;
  CoinRenderBenchmarkAnimation animation;
  if (animationEnabled) {
    const auto animationBegin = Clock::now();
    std::string diagnostic;
    if (!animation.initialize(root, camera, animationMode, animatedPercent, diagnostic)) {
      std::cerr << "Cannot prepare animation: " << diagnostic << '\n';
      root->unref(); return 2;
    }
    std::cout << "animation_detail mode=" << animationName
              << " animated_percent=" << animatedPercent
              << " eligible=" << animation.eligibleCount()
              << " selected=" << animation.selectedCount()
              << " prepared_clones=" << animation.preparedCloneCount()
              << " prepare_ms=" << elapsedMs(animationBegin, Clock::now())
              << " frame_step=" << animationStep
              << " time_step_seconds=" << double(animationStep) / 60.0
              << " selection_fnv64=0x" << std::hex << animation.selectionDigest()
              << std::dec << '\n';
  }
  const auto searchBegin = Clock::now();
  SoSearchAction searchMaterial;
  searchMaterial.setType(SoMaterial::getClassTypeId());
  searchMaterial.setInterest(SoSearchAction::FIRST);
  searchMaterial.apply(root);
  SoMaterial * animatedMaterial = searchMaterial.getPath() ?
    static_cast<SoMaterial *>(searchMaterial.getPath()->getTail()) : NULL;
  if (materialDynamic && !animatedMaterial) {
    std::cerr << "Material animation requires a SoMaterial in the scene\n";
    root->unref();
    return 2;
  }
  const SbColor initialMaterial = animatedMaterial ?
    animatedMaterial->diffuseColor[0] : SbColor(0.0f, 0.0f, 0.0f);
  const float blue = scenePath.empty() ? 0.15f : 0.1f;
  const auto setupBegin = Clock::now();
  CoinRenderSceneManager * wgpu = NULL;
  if (runWgpu) {
    wgpu = new CoinRenderSceneManager(SbVec2i32(side, side));
    wgpu->setSceneGraph(root);
    wgpu->setBackgroundColor(SbColor4f(0.1f, 0.1f, blue, 1.0f));
    wgpu->setTransparencyType(transparency == "sorted_layers" ?
      CoinRenderAction::SORTED_LAYERS_BLEND :
      CoinRenderAction::SORTED_OBJECT_BLEND);
    if (!wgpu->getRenderTarget()->setDepthReadbackEnabled(readback == "color-depth")) {
      std::cerr << "Cannot configure offscreen depth readback\n";
      root->unref();
      return 2;
    }
  }
  SoOffscreenRenderer * gl = NULL;
  if (runGl) {
    gl = new SoOffscreenRenderer(SbViewportRegion(side, side));
    gl->setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
    gl->setBackgroundColor(SbColor(0.1f, 0.1f, blue));
    gl->getGLRenderAction()->setTransparencyType(transparency == "sorted_layers" ?
      SoGLRenderAction::SORTED_LAYERS_BLEND :
      SoGLRenderAction::SORTED_OBJECT_BLEND);
  }

  const auto setupEnd = Clock::now();
  std::cout << "startup_detail material_search_ms=" << elapsedMs(searchBegin, setupBegin)
            << " target_setup_ms=" << elapsedMs(setupBegin, setupEnd) << '\n';
  if (asyncDepth == 2 && !useBgfx) {
    // Two distinct tickets are allowed in flight. Drain in submission order
    // so the benchmark never substitutes frame N-1 for frame N.
    struct Pending {
      CoinRenderReadbackTicket ticket;
      Clock::time_point submitted;
    };
    std::deque<Pending> pending;
    std::vector<double> submitMs, latencyMs;
    std::vector<uint8_t> color;
    std::vector<float> depth;
    const SbVec3f basePosition = camera->position.getValue();
    const auto updateScene = [&](int frameIndex) {
      if (dynamic) camera->position.setValue(basePosition +
        SbVec3f(float(frameIndex) * 0.0001f, 0.0f, 0.0f));
      if (materialDynamic) animatedMaterial->diffuseColor.setValue(
        0.55f + float(frameIndex % 7) * 0.035f,
        0.12f + float(frameIndex % 5) * 0.025f, 0.18f);
    };
    const size_t colorBytes = size_t(side) * size_t(side) * 4u;
    const size_t depthPixels = size_t(side) * size_t(side);
    for (int i = 0; i < warmup; ++i) {
      updateScene(i + 1);
      if (wgpu->render() != CoinRenderAction::SUCCESS) {
        std::cerr << "WebGPU async warmup failed: " << wgpu->getLastError().getString() << '\n';
        delete wgpu;
        root->unref();
        return 1;
      }
    }
    auto drainOldest = [&]() -> bool {
      const Clock::time_point deadline = Clock::now() + std::chrono::seconds(10);
      for (;;) {
        SbString diagnostic;
        const CoinRenderTarget::ReadbackStatus status =
          CoinRenderTarget::pollReadback(pending.front().ticket, color, depth, &diagnostic);
        if (status == CoinRenderTarget::READBACK_READY) {
          if (color.size() != colorBytes ||
              (readback == "color-depth" ? depth.size() != depthPixels : !depth.empty())) {
            std::cerr << "WebGPU async readback size mismatch\n";
            return false;
          }
          latencyMs.push_back(std::chrono::duration<double, std::milli>(
            Clock::now() - pending.front().submitted).count());
          pending.pop_front();
          return true;
        }
        if (status != CoinRenderTarget::READBACK_NOT_READY || Clock::now() >= deadline) {
          std::cerr << "WebGPU async readback failed: " << diagnostic.getString() << '\n';
          return false;
        }
        wgpu->getRenderTarget()->pollDevice();
        std::this_thread::yield();
      }
    };
    const Clock::time_point runBegin = Clock::now();
    for (int i = 0; i < frames; ++i) {
      updateScene(i + warmup + 1);
      CoinRenderReadbackTicket ticket{};
      const Clock::time_point begin = Clock::now();
      if (wgpu->renderAsync(ticket) != CoinRenderAction::SUCCESS) {
        std::cerr << "WebGPU async submit failed: " << wgpu->getLastError().getString() << '\n';
        delete wgpu;
        root->unref();
        return 1;
      }
      const Clock::time_point submitted = Clock::now();
      submitMs.push_back(std::chrono::duration<double, std::milli>(submitted - begin).count());
      pending.push_back(Pending{ticket, submitted});
      if (pending.size() == 2 && !drainOldest()) {
        delete wgpu;
        root->unref();
        return 1;
      }
    }
    while (!pending.empty()) {
      if (!drainOldest()) {
        delete wgpu;
        root->unref();
        return 1;
      }
    }
    const double elapsedMs = std::chrono::duration<double, std::milli>(
      Clock::now() - runBegin).count();
    std::cout << "adapter=" << caps.adapter_name << " backend=wgpu_async"
              << " depth=2 size=" << side << 'x' << side << " warmup=" << warmup
              << " scene=" << (scenePath.empty() ? "36-cubes" : scenePath)
              << " transparency=" << transparency << " mode=" << readback
              << " scene_update=" << (dynamic ? "camera-each-frame" :
                materialDynamic ? "material-each-frame" : "static") << '\n';
    report("WebGPU_async_submit", submitMs);
    report("WebGPU_async_latency", latencyMs);
    std::cout << "rgba_fnv64=0x" << std::hex
              << rgbaChecksum(color.data(), color.size()) << std::dec << '\n';
    std::cout << "WebGPU_async_throughput frames=" << frames
              << " total_ms=" << elapsedMs
              << " fps=" << double(frames) * 1000.0 / elapsedMs << '\n';
    delete wgpu;
    root->unref();
    return 0;
  }

  Clock::time_point wgpuMeasuredBegin, wgpuMeasuredEnd, glMeasuredBegin, glMeasuredEnd;
  std::vector<uint8_t> rgba;
  std::vector<uint8_t> glRgba(size_t(side) * size_t(side) * 4u);
  std::vector<float> depth;
  std::vector<double> wgpuMs, wgpuRenderMs, wgpuCopyMs, glMs;
  std::vector<double> wgpuUpdateMs, wgpuTotalMs, glUpdateMs, glRenderMs, glCopyMs, glTotalMs;
  std::vector<FrameSample> samples;
  if (!samplesOutput.empty()) samples.reserve(size_t(frames + warmup) * (runWgpu && runGl ? 2u : 1u));
  const SbVec3f basePosition = camera->position.getValue();
  const auto updateScene = [&](int frameIndex) {
    if (dynamic) camera->position.setValue(basePosition +
      SbVec3f(float(frameIndex) * 0.0001f, 0.0f, 0.0f));
    if (materialDynamic) animatedMaterial->diffuseColor.setValue(
      0.55f + float(frameIndex % 7) * 0.035f,
      0.12f + float(frameIndex % 5) * 0.025f, 0.18f);
  };
  const auto capture = [&](const char * label, int frameIndex,
                           const uint8_t * pixels, bool bottomUp) {
    if (!std::binary_search(captureFrames.begin(), captureFrames.end(), frameIndex)) return true;
    std::ostringstream path;
    path << capturePrefix << '-' << label << '-' << std::setw(6) << std::setfill('0')
         << frameIndex << ".ppm";
    uint64_t rgbHash = 0;
    if (!writeRgbImage(path.str(), pixels, side, bottomUp, &rgbHash)) {
      std::cerr << "Cannot write captured image: " << path.str() << '\n';
      return false;
    }
    std::cout << "capture backend=" << label << " frame_index=" << frameIndex
              << " logical_frame=" << int64_t(frameIndex) * animationStep
              << " rgba_fnv64=0x" << std::hex
              << rgbaChecksum(pixels, size_t(side) * side * 4u)
              << " rgb_fnv64=0x" << rgbHash
              << " state_fnv64=0x" << animation.stateDigest() << std::dec
              << " image=" << path.str() << '\n';
    return true;
  };
  for (int i = -warmup; runWgpu && i < frames; ++i) {
    const Clock::time_point updateBegin = Clock::now();
    if (animationEnabled) animation.update(int64_t(i) * animationStep);
    else updateScene(i + warmup + 1);
    const Clock::time_point begin = Clock::now();
    if (i == 0) wgpuMeasuredBegin = animationEnabled ? updateBegin : begin;
    if (wgpu->render() != CoinRenderAction::SUCCESS) {
      std::cerr << "WebGPU frame failed: " << wgpu->getLastError().getString() << '\n';
      root->unref();
      return 1;
    }
    const Clock::time_point rendered = Clock::now();
    if (rgbaOutput == "borrow") {
      std::size_t bytes = 0;
      const uint8_t * pixels = wgpu->getRenderTarget()->borrowRGBA(bytes);
      if (!pixels || bytes != size_t(side) * size_t(side) * 4u) {
        std::cerr << "WebGPU borrowed readback unavailable\n";
        root->unref();
        return 1;
      }
    } else {
      wgpu->getRenderTarget()->readbackRGBA(rgba);
      if (rgba.size() != size_t(side) * size_t(side) * 4u) {
        std::cerr << "WebGPU readback size mismatch\n";
        root->unref();
        return 1;
      }
    }
    if (readback == "color-depth") {
      wgpu->getRenderTarget()->readbackDepth(depth);
      if (depth.size() != size_t(side) * size_t(side)) {
        std::cerr << "WebGPU depth readback size mismatch\n";
        root->unref();
        return 1;
      }
    }
    const Clock::time_point end = Clock::now();
    if (i == -warmup) {
      std::cout << rendererLabel << "_first_detail render_ms=" << elapsedMs(begin, rendered)
                << " publication_copy_ms=" << elapsedMs(rendered, end)
                << " update_ms=" << elapsedMs(updateBegin, begin)
                << " total_with_update_ms=" << elapsedMs(updateBegin, end)
                << " before_frame_since_main_ms=" << elapsedMs(mainBegin, begin)
                << " result_since_main_ms=" << elapsedMs(mainBegin, end) << '\n';
      std::cout << rendererLabel << "_first_frame_ms="
                << std::chrono::duration<double, std::milli>(end - begin).count()
                << std::endl;
    }
    if (i >= 0) {
      wgpuMeasuredEnd = end;
      wgpuMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      wgpuRenderMs.push_back(std::chrono::duration<double, std::milli>(rendered - begin).count());
      wgpuCopyMs.push_back(std::chrono::duration<double, std::milli>(end - rendered).count());
      wgpuUpdateMs.push_back(elapsedMs(updateBegin, begin));
      wgpuTotalMs.push_back(elapsedMs(updateBegin, end));
    }
    if (!samplesOutput.empty()) samples.push_back(FrameSample{rendererLabel, i,
      int64_t(i) * animationStep, i < 0, elapsedMs(updateBegin, begin),
      elapsedMs(begin, rendered), elapsedMs(rendered, end), elapsedMs(updateBegin, end)});
    if (i >= 0 && !capture(rendererLabel, i, rgba.data(), false)) {
      root->unref(); delete gl; delete wgpu; return 1;
    }
  }
  if (runWgpu && animationEnabled)
    std::cout << rendererLabel << "_animation_final logical_frame=" << int64_t(frames - 1) * animationStep
              << " state_fnv64=0x" << std::hex << animation.stateDigest() << std::dec << '\n';
  if (runWgpu && runGl && animationEnabled) animation.restore();
  camera->position.setValue(basePosition);
  if (materialDynamic) animatedMaterial->diffuseColor.setValue(initialMaterial);
  for (int i = -warmup; runGl && i < frames; ++i) {
    const Clock::time_point updateBegin = Clock::now();
    if (animationEnabled) animation.update(int64_t(i) * animationStep);
    else updateScene(i + warmup + 1);
    const Clock::time_point begin = Clock::now();
    if (i == 0) glMeasuredBegin = animationEnabled ? updateBegin : begin;
    if (!gl->render(root)) {
      std::cerr << "Coin/GL offscreen context or readback unavailable\n";
      root->unref();
      return 2;
    }
    const auto rendered = Clock::now();
    const unsigned char * pixels = gl->getBuffer();
    const auto fetched = Clock::now();
    if (!pixels) {
      std::cerr << "Coin/GL offscreen RGBA buffer unavailable\n";
      root->unref();
      return 2;
    }
    std::memcpy(glRgba.data(), pixels, glRgba.size());
    const Clock::time_point end = Clock::now();
    if (i == -warmup) {
      std::cout << "CoinGL_first_detail render_ms=" << elapsedMs(begin, rendered)
                << " lazy_readback_ms=" << elapsedMs(rendered, fetched)
                << " publication_copy_ms=" << elapsedMs(fetched, end)
                << " update_ms=" << elapsedMs(updateBegin, begin)
                << " total_with_update_ms=" << elapsedMs(updateBegin, end)
                << " before_frame_since_main_ms=" << elapsedMs(mainBegin, begin)
                << " result_since_main_ms=" << elapsedMs(mainBegin, end) << '\n';
      std::cout << "CoinGL_first_frame_ms="
                << std::chrono::duration<double, std::milli>(end - begin).count()
                << std::endl;
    }
    if (i >= 0) {
      glMeasuredEnd = end;
      glMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      glUpdateMs.push_back(elapsedMs(updateBegin, begin));
      glRenderMs.push_back(elapsedMs(begin, fetched));
      glCopyMs.push_back(elapsedMs(fetched, end));
      glTotalMs.push_back(elapsedMs(updateBegin, end));
    }
    if (!samplesOutput.empty()) samples.push_back(FrameSample{"CoinGL", i,
      int64_t(i) * animationStep, i < 0, elapsedMs(updateBegin, begin),
      elapsedMs(begin, fetched), elapsedMs(fetched, end), elapsedMs(updateBegin, end)});
    if (i >= 0 && !capture("CoinGL", i, glRgba.data(), true)) {
      root->unref(); delete gl; delete wgpu; return 1;
    }
  }
  if (runGl && animationEnabled)
    std::cout << "CoinGL_animation_final logical_frame=" << int64_t(frames - 1) * animationStep
              << " state_fnv64=0x" << std::hex << animation.stateDigest() << std::dec << '\n';
  if (!samplesOutput.empty()) {
    std::ofstream output(samplesOutput.c_str());
    output << "backend,frame_index,logical_frame,warmup,update_ms,render_ms,publication_ms,total_ms\n"
           << std::setprecision(17);
    for (const auto & sample : samples)
      output << sample.backend << ',' << sample.index << ',' << sample.logicalFrame << ','
             << (sample.warmup ? 1 : 0) << ',' << sample.updateMs << ',' << sample.renderMs
             << ',' << sample.publicationMs << ',' << sample.totalMs << '\n';
    output.close();
    if (!output) {
      std::cerr << "Cannot write samples: " << samplesOutput << '\n';
      root->unref(); delete gl; delete wgpu; return 1;
    }
    std::cout << "samples_output=" << samplesOutput << " rows=" << samples.size() << '\n';
  }
  if (runWgpu) {
    const size_t bytes = size_t(side) * size_t(side) * 4u;
    const uint8_t * pixels = rgba.data();
    if (rgbaOutput == "borrow") {
      std::size_t borrowedBytes = 0;
      pixels = wgpu->getRenderTarget()->borrowRGBA(borrowedBytes);
      if (!pixels || borrowedBytes != bytes) return 1;
    }
    std::cout << "rgba_fnv64=0x" << std::hex
              << rgbaChecksum(pixels, bytes) << std::dec << '\n';
  }
  if (runGl)
    std::cout << "gl_rgba_fnv64=0x" << std::hex
              << rgbaChecksum(glRgba.data(), glRgba.size()) << std::dec << '\n';
  if (!imageOutput.empty()) {
    // PPM rows are top-down; the legacy GL readback is bottom-up.
    const auto& pixels = runWgpu ? rgba : glRgba;
    if (!writeRgbImage(imageOutput, pixels.data(), side, !runWgpu)) {
      std::cerr << "Cannot write image: " << imageOutput << '\n';
      root->unref(); delete gl; delete wgpu; return 1;
    }
    std::cout << "image=" << imageOutput << '\n';
  }
  root->unref();
  std::cout << "adapter=" << (runWgpu ?
              caps.adapter_name : "not-queried")
            << " vendor_id=0x" << std::hex << (runWgpu ? caps.vendor_id : 0)
            << " device_id=0x" << (runWgpu ? caps.device_id : 0) << std::dec
            << " backend=" << backend << " size=" << side << 'x' << side
            << " warmup=" << warmup << " scene="
            << (scenePath.empty() ? "36-cubes" : scenePath)
            << " transparency=" << transparency
            << " mode=" << (readback == "color" ? "render+rgba-readback" :
                             "render+rgba+depth-readback")
            << " pipeline_depth=" << (useBgfx ? (asyncDepth ? asyncDepth : 1) : 0)
            << " rgba_output=" << rgbaOutput
            << " scene_update=" << (animationEnabled ? animationName.c_str() : dynamic ? "camera-each-frame" :
                materialDynamic ? "material-each-frame" : "static") << '\n';
  std::cout << "timing_detail legacy_frame_scope=render+readback+publication"
            << " total_scope=update+render+readback+publication"
            << " render_scope=render+readback experimental_publication=" << rgbaOutput
            << " coingl_publication=copy"
            << " measured_indices=0.." << frames - 1
            << " warmup_indices=-" << warmup << "..-1"
            << " frame_step=" << animationStep
            << " captures=" << captureFrames.size()
            << " capture_io=outside_frame_timers"
            << " wall_throughput_includes_capture=" << (!captureFrames.empty() ? 1 : 0)
            << " wall_throughput_scope=" << (animationEnabled ? "update+frame" : "legacy-frame")
            << " new_stats_percentile=nearest_rank new_stats_median=average_middle"
            << " legacy_stats_median=upper_middle"
            << " budget_60hz_ms=" << 1000.0 / 60.0
            << " budget_30hz_ms=" << 1000.0 / 30.0 << '\n';
  if (runWgpu) {
    report(rendererLabel, wgpuMs);
    report(caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ?
           "BGFX_render_and_readback" : "WebGPU_render", wgpuRenderMs);
    report(caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION ?
           "BGFX_publication_copy" : "WebGPU_copy", wgpuCopyMs);
    reportTiming(std::string(rendererLabel) + "_timing_total", wgpuTotalMs);
    reportTiming(std::string(rendererLabel) + "_timing_update", wgpuUpdateMs);
    reportTiming(std::string(rendererLabel) + "_timing_render", wgpuRenderMs);
    std::cout << rendererLabel << "_throughput frames=" << frames
              << " total_ms=" << std::chrono::duration<double, std::milli>(
                   wgpuMeasuredEnd - wgpuMeasuredBegin).count()
              << " fps=" << double(frames) * 1000.0 /
                   std::chrono::duration<double, std::milli>(
                     wgpuMeasuredEnd - wgpuMeasuredBegin).count() << '\n';
  }
  if (runGl) {
    report("CoinGL", glMs);
    reportTiming("CoinGL_timing_total", glTotalMs);
    reportTiming("CoinGL_timing_update", glUpdateMs);
    reportTiming("CoinGL_timing_render", glRenderMs);
    report("CoinGL_publication_copy", glCopyMs);
    const double elapsedMs = std::chrono::duration<double, std::milli>(
      glMeasuredEnd - glMeasuredBegin).count();
    std::cout << "CoinGL_throughput frames=" << frames << " total_ms=" << elapsedMs
              << " fps=" << double(frames) * 1000.0 / elapsedMs << '\n';
  }
  CoinRenderCacheTelemetry cache;
  if (runWgpu && wgpu->getRenderTarget()->getCacheTelemetry(cache)) {
    std::cout << "WebGPU_cache last_frame_uploads=" << cache.frameUploads
              << " last_frame_uploaded_bytes=" << cache.frameUploadedBytes
              << " last_frame_hits=" << cache.frameHits
              << " cumulative_uploads=" << cache.cumulativeUploads
              << " cumulative_uploaded_bytes=" << cache.cumulativeUploadedBytes
              << " cumulative_hits=" << cache.cumulativeHits << '\n';
  }
  std::cout << "backend_cleanup_begin" << std::endl;
  const Clock::time_point cleanupBegin = Clock::now();
  delete gl;
  delete wgpu;
  std::cout << "backend_cleanup_ms=" << std::chrono::duration<double, std::milli>(
    Clock::now() - cleanupBegin).count() << std::endl;
  return 0;
}
