// Render normalized FreeCAD example meshes in the experimental WebGPU path
// and the Coin/GL reference. The original FreeCAD viewport is not involved.

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/SbRotation.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderSceneManager.h>
#include <Inventor/rendering/CoinRenderTarget.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool writePpm(const std::string & path, const std::vector<uint8_t> & rgba,
              int side) {
  std::ofstream stream(path.c_str(), std::ios::binary);
  if (!stream) return false;
  stream << "P6\n" << side << ' ' << side << "\n255\n";
  for (size_t i = 0; i < rgba.size(); i += 4)
    stream.write(reinterpret_cast<const char *>(&rgba[i]), 3);
  return stream.good();
}

bool foreground(const uint8_t * rgba) {
  return std::abs(int(rgba[0]) - 26) +
         std::abs(int(rgba[1]) - 26) +
         std::abs(int(rgba[2]) - 26) > 36;
}

struct Metrics { double mae; double iou; };

Metrics compare(const std::vector<uint8_t> & wgpu,
                const std::vector<uint8_t> & gl, int side, bool flip) {
  uint64_t difference = 0, unionCount = 0, intersection = 0;
  uint64_t wgpuForeground = 0, glForeground = 0;
  for (int y = 0; y < side; ++y) {
    const int glY = flip ? side - 1 - y : y;
    for (int x = 0; x < side; ++x) {
      const uint8_t * a = &wgpu[(size_t(y) * side + x) * 4];
      const uint8_t * b = &gl[(size_t(glY) * side + x) * 4];
      for (int channel = 0; channel < 3; ++channel)
        difference += std::abs(int(a[channel]) - int(b[channel]));
      const bool fa = foreground(a), fb = foreground(b);
      wgpuForeground += fa;
      glForeground += fb;
      unionCount += fa || fb;
      intersection += fa && fb;
    }
  }
  const double mae = double(difference) / (double(side) * side * 3.0);
  const double iou = unionCount ? double(intersection) / unionCount : 0.0;
  std::cout << (flip ? "vertical_flip" : "same_orientation")
            << " mae_rgb=" << mae << " silhouette_iou=" << iou
            << " foreground_wgpu=" << wgpuForeground
            << " foreground_gl=" << glForeground << '\n';
  return Metrics{mae, iou};
}
}

int main(int argc, char ** argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "Usage: coin_render_freecad_compare scene.iv output-prefix side [lit]\n";
    return 2;
  }
  const int side = std::atoi(argv[3]);
  const bool lit = argc == 5 && std::string(argv[4]) == "lit";
  if (side < 32 || side > 2048 || (argc == 5 && !lit)) return 2;

  SoDB::init();
  CoinRenderAction::initClass();
  CoinRenderCapabilities caps{};
  if (coin_render_query_capabilities(
        COIN_RENDER_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0) {
    std::cerr << "Experimental offscreen capabilities unavailable\n";
    return 2;
  }
  const bool bgfx = caps.backend == COIN_RENDER_EXPERIMENTAL_BGFX_EVALUATION;
  if (!caps.gpu_available ||
      (!bgfx && caps.backend != COIN_RENDER_EXPERIMENTAL_RUST)) {
    std::cerr << "Requested offscreen renderer/profile unavailable: "
              << caps.diagnostic << '\n';
    return 2;
  }

  SoInput input;
  if (!input.openFile(argv[1])) {
    std::cerr << "Cannot open normalized Inventor scene\n";
    return 2;
  }
  SoSeparator * imported = SoDB::readAll(&input);
  if (!imported || imported->getNumChildren() == 0) {
    std::cerr << "Cannot read normalized Inventor scene\n";
    return 2;
  }
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoPerspectiveCamera * camera = new SoPerspectiveCamera;
  camera->orientation.setValue(SbRotation(SbVec3f(0.0f, 0.0f, -1.0f),
                                          SbVec3f(-0.5f, -0.35f, -1.0f)));
  scene->addChild(camera);
  SoLightModel * model = new SoLightModel;
  model->model = lit ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
  scene->addChild(model);
  if (lit) {
    SoDirectionalLight * light = new SoDirectionalLight;
    light->direction.setValue(-0.4f, -0.6f, -1.0f);
    light->intensity = 0.8f;
    scene->addChild(light);
  }
  scene->addChild(imported);
  const SbViewportRegion viewport(side, side);
  camera->viewAll(scene, viewport, 1.15f);

  CoinRenderSceneManager wgpu(SbVec2i32(side, side));
  wgpu.setSceneGraph(scene);
  wgpu.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.1f, 1.0f));
  if (!wgpu.getRenderTarget()->setDepthReadbackEnabled(FALSE)) {
    std::cerr << "Cannot configure RGBA-only WebGPU target\n";
    scene->unref();
    return 1;
  }
  const CoinRenderAction::Status status = wgpu.render();
  if (status != CoinRenderAction::SUCCESS) {
    std::cerr << "WebGPU failed with status " << int(status) << ": "
              << wgpu.getLastError().getString() << '\n';
    scene->unref();
    return 1;
  }
  std::vector<uint8_t> webgpuPixels;
  wgpu.getRenderTarget()->readbackRGBA(webgpuPixels);

  SoOffscreenRenderer gl(viewport);
  gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
  gl.setBackgroundColor(SbColor(0.1f, 0.1f, 0.1f));
  if (!gl.render(scene) || !gl.getBuffer()) {
    std::cerr << "Coin/GL context or readback unavailable\n";
    scene->unref();
    return 2;
  }
  const size_t expected = size_t(side) * size_t(side) * 4u;
  if (webgpuPixels.size() != expected) {
    std::cerr << "WebGPU readback size mismatch\n";
    scene->unref();
    return 1;
  }
  const unsigned char * glBuffer = gl.getBuffer();
  std::vector<uint8_t> glPixels(glBuffer, glBuffer + expected);
  scene->unref();
  if (!writePpm(std::string(argv[2]) + (bgfx ? "-bgfx.ppm" : "-wgpu.ppm"), webgpuPixels, side) ||
      !writePpm(std::string(argv[2]) + "-gl.ppm", glPixels, side)) {
    std::cerr << "Cannot write comparison images\n";
    return 2;
  }
  std::cout << "adapter=" << caps.adapter_name
            << " side=" << side
            << " mode=" << (lit ? "lit" : "base_color") << '\n';
  compare(webgpuPixels, glPixels, side, false);
  const Metrics aligned = compare(webgpuPixels, glPixels, side, true);
  if (!lit && (aligned.mae > 1.0 || aligned.iou < 0.99)) {
    std::cerr << "BASE_COLOR parity gate failed after GL row inversion\n";
    return 1;
  }
  return 0;
}
