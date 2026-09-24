// Opt-in end-to-end offscreen comparison. Both paths include RGBA readback;
// these numbers are observations on one adapter/driver, never a speed SLA.

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
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
#include <iostream>
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

SoSeparator * createScene() {
  SoSeparator * root = new SoSeparator;
  root->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
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
}

int main(int argc, char ** argv) {
  int frames = 30;
  int warmup = 8;
  int side = 256;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) side = std::atoi(argv[++i]);
    else {
      std::cerr << "Usage: wgpu_gl_benchmark [--frames 30] [--warmup 8] [--size 256]\n";
      return 2;
    }
  }
  if (frames < 1 || frames > 10000 || warmup < 0 || warmup > 10000 ||
      side < 1 || side > 2048) {
    std::cerr << "Invalid benchmark dimensions or sample count\n";
    return 2;
  }

  SoDB::init();
  SoWgpuRenderAction::initClass();
  CoinWgpuExperimentalCapabilities caps{};
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0 ||
      caps.backend != COIN_WGPU_EXPERIMENTAL_RUST || !caps.gpu_available) {
    std::cerr << "Rust WebGPU offscreen adapter unavailable\n";
    return 2;
  }

  SoSeparator * root = createScene();
  SoWgpuSceneManager wgpu(SbVec2i32(side, side));
  wgpu.setSceneGraph(root);
  wgpu.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.15f, 1.0f));
  SoOffscreenRenderer gl(SbViewportRegion(side, side));
  gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
  gl.setBackgroundColor(SbColor(0.1f, 0.1f, 0.15f));

  std::vector<uint8_t> rgba;
  std::vector<double> wgpuMs, glMs;
  for (int i = -warmup; i < frames; ++i) {
    const Clock::time_point begin = Clock::now();
    if (wgpu.render() != SoWgpuRenderAction::SUCCESS) {
      std::cerr << "WebGPU frame failed: " << wgpu.getLastError().getString() << '\n';
      root->unref();
      return 1;
    }
    wgpu.getRenderTarget()->readbackRGBA(rgba);
    if (rgba.size() != size_t(side) * size_t(side) * 4u) {
      std::cerr << "WebGPU readback size mismatch\n";
      root->unref();
      return 1;
    }
    const Clock::time_point end = Clock::now();
    if (i >= 0) wgpuMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  for (int i = -warmup; i < frames; ++i) {
    const Clock::time_point begin = Clock::now();
    if (!gl.render(root) || !gl.getBuffer()) {
      std::cerr << "Coin/GL offscreen context or readback unavailable\n";
      root->unref();
      return 2;
    }
    const Clock::time_point end = Clock::now();
    if (i >= 0) glMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  root->unref();
  std::cout << "adapter=" << caps.adapter_name << " size=" << side << 'x' << side
            << " warmup=" << warmup << " scene=36-cubes mode=render+rgba-readback\n";
  report("WebGPU", wgpuMs);
  report("CoinGL", glMs);
  return 0;
}
