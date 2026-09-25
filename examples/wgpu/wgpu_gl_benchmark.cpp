// Opt-in end-to-end offscreen comparison. Both paths include RGBA readback;
// these numbers are observations on one adapter/driver, never a speed SLA.

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbRotation.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoCube.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

void report(const char * backend, const std::vector<double> & values) {
  std::vector<double> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const size_t median = sorted.size() / 2;
  const size_t p95 = (sorted.size() - 1) * 95 / 100;
  std::cout << backend << " frames=" << sorted.size()
            << " median_ms=" << sorted[median]
            << " p95_ms=" << sorted[p95]
            << " min_ms=" << sorted.front()
            << " max_ms=" << sorted.back() << '\n';
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
  if (!input.openFile(path.c_str())) return NULL;
  SoSeparator * imported = SoDB::readAll(&input);
  if (!imported || imported->getNumChildren() == 0) return NULL;

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
  camera->viewAll(root, SbViewportRegion(side, side), 1.15f);
  return root;
}
}

int main(int argc, char ** argv) {
  int frames = 30;
  int warmup = 8;
  int side = 256;
  int asyncDepth = 0;
  bool dynamic = false;
  std::string backend = "both";
  std::string readback = "color";
  std::string rgbaOutput = "copy";
  std::string scenePath;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) side = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--async-depth") == 0 && i + 1 < argc) asyncDepth = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) scenePath = argv[++i];
    else if (std::strcmp(argv[i], "--dynamic") == 0) dynamic = true;
    else if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) backend = argv[++i];
    else if (std::strcmp(argv[i], "--readback") == 0 && i + 1 < argc) readback = argv[++i];
    else if (std::strcmp(argv[i], "--rgba-output") == 0 && i + 1 < argc) rgbaOutput = argv[++i];
    else {
      std::cerr << "Usage: wgpu_gl_benchmark [--frames 30] [--warmup 8]"
                   " [--size 256] [--scene normalized.iv] [--dynamic]"
                   " [--backend both|wgpu|gl] [--readback color|color-depth] [--rgba-output copy|borrow] [--async-depth 2]\n";
      return 2;
    }
  }
  if (frames < 1 || frames > 10000 || warmup < 0 || warmup > 10000 ||
      side < 1 || side > 2048 ||
      (backend != "both" && backend != "wgpu" && backend != "gl") ||
      (readback != "color" && readback != "color-depth") ||
      (rgbaOutput != "copy" && rgbaOutput != "borrow") ||
      (asyncDepth != 0 && asyncDepth != 2) ||
      (asyncDepth != 0 && (backend != "wgpu" || rgbaOutput != "copy"))) {
    std::cerr << "Invalid benchmark dimensions or sample count\n";
    return 2;
  }
  if (readback == "color-depth" && backend != "wgpu") {
    std::cerr << "Color+depth readback is WebGPU-only; GL comparison reads color\n";
    return 2;
  }

  SoDB::init();
  SoWgpuRenderAction::initClass();
  CoinWgpuExperimentalCapabilities caps{};
  const bool runWgpu = backend != "gl";
  const bool runGl = backend != "wgpu";
  if (runWgpu) {
    if (coin_wgpu_experimental_query_capabilities(
          COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0 ||
        caps.backend != COIN_WGPU_EXPERIMENTAL_RUST || !caps.gpu_available) {
      std::cerr << "Rust WebGPU offscreen adapter unavailable\n";
      return 2;
    }
  }

  SoPerspectiveCamera * camera = NULL;
  SoSeparator * root = scenePath.empty() ? createScene(&camera) :
    loadScene(scenePath, side, &camera);
  if (!root) {
    std::cerr << "Cannot read normalized Inventor scene\n";
    return 2;
  }
  const float blue = scenePath.empty() ? 0.15f : 0.1f;
  SoWgpuSceneManager * wgpu = NULL;
  if (runWgpu) {
    wgpu = new SoWgpuSceneManager(SbVec2i32(side, side));
    wgpu->setSceneGraph(root);
    wgpu->setBackgroundColor(SbColor4f(0.1f, 0.1f, blue, 1.0f));
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
  }

  if (asyncDepth == 2) {
    // Two distinct tickets are allowed in flight. Drain in submission order
    // so the benchmark never substitutes frame N-1 for frame N.
    struct Pending {
      SoWgpuReadbackTicket ticket;
      Clock::time_point submitted;
    };
    std::deque<Pending> pending;
    std::vector<double> submitMs, latencyMs;
    std::vector<uint8_t> color;
    std::vector<float> depth;
    const SbVec3f basePosition = camera->position.getValue();
    const size_t colorBytes = size_t(side) * size_t(side) * 4u;
    const size_t depthPixels = size_t(side) * size_t(side);
    for (int i = 0; i < warmup; ++i) {
      if (dynamic) {
        const float offset = float(i + 1) * 0.0001f;
        camera->position.setValue(basePosition + SbVec3f(offset, 0.0f, 0.0f));
      }
      if (wgpu->render() != SoWgpuRenderAction::SUCCESS) {
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
        const SoWgpuRenderTarget::ReadbackStatus status =
          SoWgpuRenderTarget::pollReadback(pending.front().ticket, color, depth, &diagnostic);
        if (status == SoWgpuRenderTarget::READBACK_READY) {
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
        if (status != SoWgpuRenderTarget::READBACK_NOT_READY || Clock::now() >= deadline) {
          std::cerr << "WebGPU async readback failed: " << diagnostic.getString() << '\n';
          return false;
        }
        wgpu->getRenderTarget()->pollDevice();
        std::this_thread::yield();
      }
    };
    const Clock::time_point runBegin = Clock::now();
    for (int i = 0; i < frames; ++i) {
      if (dynamic) {
        const float offset = float(i + warmup + 1) * 0.0001f;
        camera->position.setValue(basePosition + SbVec3f(offset, 0.0f, 0.0f));
      }
      SoWgpuReadbackTicket ticket{};
      const Clock::time_point begin = Clock::now();
      if (wgpu->renderAsync(ticket) != SoWgpuRenderAction::SUCCESS) {
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
              << " mode=" << readback
              << " scene_update=" << (dynamic ? "camera-each-frame" : "static") << '\n';
    report("WebGPU_async_submit", submitMs);
    report("WebGPU_async_latency", latencyMs);
    std::cout << "WebGPU_async_throughput frames=" << frames
              << " total_ms=" << elapsedMs
              << " fps=" << double(frames) * 1000.0 / elapsedMs << '\n';
    delete wgpu;
    root->unref();
    return 0;
  }

  double wgpuMeasuredTotalMs = 0.0;
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  std::vector<double> wgpuMs, wgpuRenderMs, wgpuCopyMs, glMs;
  const SbVec3f basePosition = camera->position.getValue();
  for (int i = -warmup; runWgpu && i < frames; ++i) {
    if (dynamic) {
      const float offset = float(i + warmup + 1) * 0.0001f;
      camera->position.setValue(basePosition + SbVec3f(offset, 0.0f, 0.0f));
    }
    const Clock::time_point begin = Clock::now();
    if (wgpu->render() != SoWgpuRenderAction::SUCCESS) {
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
    if (i >= 0) {
      wgpuMeasuredTotalMs += std::chrono::duration<double, std::milli>(end - begin).count();
      wgpuMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
      wgpuRenderMs.push_back(std::chrono::duration<double, std::milli>(rendered - begin).count());
      wgpuCopyMs.push_back(std::chrono::duration<double, std::milli>(end - rendered).count());
    }
  }
  camera->position.setValue(basePosition);
  for (int i = -warmup; runGl && i < frames; ++i) {
    if (dynamic) {
      const float offset = float(i + warmup + 1) * 0.0001f;
      camera->position.setValue(basePosition + SbVec3f(offset, 0.0f, 0.0f));
    }
    const Clock::time_point begin = Clock::now();
    if (!gl->render(root) || !gl->getBuffer()) {
      std::cerr << "Coin/GL offscreen context or readback unavailable\n";
      root->unref();
      return 2;
    }
    const Clock::time_point end = Clock::now();
    if (i >= 0) glMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  root->unref();
  std::cout << "adapter=" << (runWgpu ? caps.adapter_name : "not-queried")
            << " backend=" << backend << " size=" << side << 'x' << side
            << " warmup=" << warmup << " scene="
            << (scenePath.empty() ? "36-cubes" : scenePath)
            << " mode=" << (readback == "color" ? "render+rgba-readback" :
                             "render+rgba+depth-readback")
            << " rgba_output=" << rgbaOutput
            << " scene_update=" << (dynamic ? "camera-each-frame" : "static") << '\n';
  if (runWgpu) {
    report("WebGPU", wgpuMs);
    report("WebGPU_render", wgpuRenderMs);
    report("WebGPU_copy", wgpuCopyMs);
    std::cout << "WebGPU_throughput frames=" << frames
              << " total_ms=" << wgpuMeasuredTotalMs
              << " fps=" << double(frames) * 1000.0 / wgpuMeasuredTotalMs << '\n';
  }
  if (runGl) report("CoinGL", glMs);
  SoWgpuCacheTelemetry cache;
  if (runWgpu && wgpu->getRenderTarget()->getCacheTelemetry(cache)) {
    std::cout << "WebGPU_cache last_frame_uploads=" << cache.frameUploads
              << " last_frame_uploaded_bytes=" << cache.frameUploadedBytes
              << " last_frame_hits=" << cache.frameHits
              << " cumulative_uploads=" << cache.cumulativeUploads
              << " cumulative_uploaded_bytes=" << cache.cumulativeUploadedBytes
              << " cumulative_hits=" << cache.cumulativeHits << '\n';
  }
  delete gl;
  delete wgpu;
  return 0;
}
