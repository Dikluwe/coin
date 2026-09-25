#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
bool check(bool condition, const char * message)
{
  if (!condition) std::cerr << "WgpuBackendContractTest: " << message << '\n';
  return condition;
}

bool checkPixels(SoWgpuRenderTarget * target, int width, int height)
{
  const size_t expected = size_t(width) * size_t(height) * 4u;
  std::vector<uint8_t> color;
  target->readbackRGBA(color);
  size_t borrowedBytes = 0;
  const uint8_t * borrowed = target->borrowRGBA(borrowedBytes);
  if (!check(color.size() == expected && borrowed != NULL &&
             borrowedBytes == expected &&
             std::equal(color.begin(), color.end(), borrowed),
             "copy and borrowed RGBA disagree")) return false;

  const size_t center = (size_t(height / 2) * size_t(width) +
                         size_t(width / 2)) * 4u;
  const size_t corner = (size_t(4) * size_t(width) + 4u) * 4u;
  if (!check(color[center] > 150 && color[center + 1] < 40 &&
             color[center + 2] < 40,
             "BASE_COLOR cube center is not red") ||
      !check(color[corner] >= 20 && color[corner] <= 35 &&
             color[corner + 1] >= 20 && color[corner + 1] <= 35 &&
             color[corner + 2] >= 20 && color[corner + 2] <= 35,
             "background clear color is wrong")) return false;

  std::vector<float> depth;
  target->readbackDepth(depth);
  return check(depth.empty(), "disabled depth readback published pixels");
}
}

int main()
{
  SoDB::init();
  SoWgpuRenderAction::initClass();

  CoinWgpuExperimentalCapabilities caps{};
  if (!check(coin_wgpu_experimental_query_capabilities(
               COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) == 0,
             "offscreen capability query failed")) return 1;
  if (caps.backend == COIN_WGPU_EXPERIMENTAL_NATIVE_SPIKE) {
    std::cout << "Native spike has no established rendering profile\n";
    return 77;
  }
  const uint64_t common = COIN_WGPU_FEATURE_TRIANGLES |
                          COIN_WGPU_FEATURE_INDEXED_GEOMETRY;
  if (!check((caps.features & common) == common,
             "backend does not advertise the shared triangle profile")) return 1;
  if (caps.backend == COIN_WGPU_EXPERIMENTAL_RUST && !caps.gpu_available) {
    std::cout << "WebGPU adapter unavailable\n";
    return 77;
  }

  SoSeparator * root = new SoSeparator;
  root->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 4.0f);
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  camera->height = 2.0f;
  root->addChild(camera);
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  root->addChild(lightModel);
  root->addChild(new SoDirectionalLight);
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(0.9f, 0.0f, 0.0f);
  root->addChild(material);
  SoCube * cube = new SoCube;
  cube->width = 1.0f;
  cube->height = 1.0f;
  cube->depth = 1.0f;
  root->addChild(cube);

  SoWgpuSceneManager manager(SbVec2i32(64, 64));
  manager.setSceneGraph(root);
  root->unref();
  manager.setBackgroundColor(SbColor4f(0.1f, 0.1f, 0.1f, 1.0f));
  SoWgpuRenderTarget * target = manager.getRenderTarget();
  if (!check(target != NULL && target->setDepthReadbackEnabled(FALSE),
             "cannot select color-only output")) return 1;
  SoWgpuRenderAction::Status status = manager.render();
  if (status == SoWgpuRenderAction::NOT_READY &&
      caps.backend == COIN_WGPU_EXPERIMENTAL_BGFX_EVALUATION) {
    std::cout << "BGFX renderer unavailable: "
              << manager.getLastError().getString() << '\n';
    return 77;
  }
  if (!check(status == SoWgpuRenderAction::SUCCESS,
             manager.getLastError().getString()) ||
      !checkPixels(target, 64, 64)) return 1;

  if (!check(manager.resize(SbVec2i32(96, 64)),
             "resize failed")) return 1;
  size_t staleBytes = 1;
  if (!check(target->borrowRGBA(staleBytes) == NULL && staleBytes == 0,
             "resize did not invalidate borrowed pixels") ||
      !check(manager.render() == SoWgpuRenderAction::SUCCESS,
             manager.getLastError().getString()) ||
      !checkPixels(target, 96, 64)) return 1;

  // A feature absent from the advertised profile must fail explicitly;
  // backends that advertise it must render the same Coin scene successfully.
  lightModel->model = SoLightModel::PHONG;
  status = manager.render();
  if (caps.features & COIN_WGPU_FEATURE_LIGHTS) {
    if (!check(status == SoWgpuRenderAction::SUCCESS,
               manager.getLastError().getString()) ||
        !checkPixels(target, 96, 64)) return 1;
  } else if (!check(status == SoWgpuRenderAction::UNSUPPORTED,
                    "unadvertised lighting was silently accepted")) return 1;

  std::cout << "Shared offscreen rendering contract passed for backend "
            << caps.backend << '\n';
  return 0;
}
