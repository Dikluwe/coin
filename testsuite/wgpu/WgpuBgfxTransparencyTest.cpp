#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoWgpuRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/SoWgpuCapabilities.h>
#include <Inventor/rendering/SoWgpuRenderTarget.h>
#include <Inventor/rendering/SoWgpuSceneManager.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
const int side = 128;
using Rgb = std::array<int, 3>;

void addQuad(SoSeparator * scene, const Rgb & color, float alpha,
             float leftZ, float rightZ)
{
  SoSeparator * object = new SoSeparator;
  SoMaterial * material = new SoMaterial;
  material->diffuseColor.setValue(color[0] / 255.0f, color[1] / 255.0f,
                                  color[2] / 255.0f);
  material->transparency.setValue(1.0f - alpha);
  object->addChild(material);
  SoCoordinate3 * coordinates = new SoCoordinate3;
  coordinates->point.set1Value(0, SbVec3f(-0.8f, -0.8f, leftZ));
  coordinates->point.set1Value(1, SbVec3f( 0.8f, -0.8f, rightZ));
  coordinates->point.set1Value(2, SbVec3f( 0.8f,  0.8f, rightZ));
  coordinates->point.set1Value(3, SbVec3f(-0.8f,  0.8f, leftZ));
  object->addChild(coordinates);
  SoIndexedFaceSet * face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, 3, -1};
  face->coordIndex.setValues(0, 5, indices);
  object->addChild(face);
  scene->addChild(object);
}

SoSeparator * makeScene(int scenario)
{
  const bool crossing = scenario == 1;
  const bool occluded = scenario == 2;
  SoSeparator * scene = new SoSeparator;
  scene->ref();
  SoOrthographicCamera * camera = new SoOrthographicCamera;
  camera->position.setValue(0.0f, 0.0f, 5.0f);
  camera->height = 2.0f;
  camera->nearDistance = 0.1f;
  camera->farDistance = 10.0f;
  scene->addChild(camera);
  SoLightModel * lightModel = new SoLightModel;
  lightModel->model = SoLightModel::BASE_COLOR;
  scene->addChild(lightModel);
  SoTransparencyType * transparency = new SoTransparencyType;
  transparency->value = SoTransparencyType::SORTED_OBJECT_BLEND;
  scene->addChild(transparency);
  // Deliberately not back-to-front: both renderers must schedule the draws.
  addQuad(scene, {{255, 0, 0}}, 0.5f,
          crossing ? 0.4f : 0.0f, crossing ? -0.4f : 0.0f);
  addQuad(scene, {{0, 0, 255}}, 1.0f,
          occluded ? 0.8f : -1.0f, occluded ? 0.8f : -1.0f);
  addQuad(scene, {{0, 255, 0}}, 0.5f,
          crossing ? -0.4f : -0.5f, crossing ? 0.4f : -0.5f);
  return scene;
}

Rgb pixel(const std::vector<uint8_t> & image, int x, int y, int channels,
          bool bottomUp)
{
  const int row = bottomUp ? side - 1 - y : y;
  const size_t offset = (size_t(row) * side + size_t(x)) * size_t(channels);
  return {{image[offset], image[offset + 1], image[offset + 2]}};
}

bool writePpm(const std::string & path, const std::vector<uint8_t> & image,
              int channels, bool bottomUp)
{
  std::ofstream out(path.c_str(), std::ios::binary);
  if (!out) return false;
  out << "P6\n" << side << ' ' << side << "\n255\n";
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      const Rgb rgb = pixel(image, x, y, channels, bottomUp);
      const char bytes[3] = {char(rgb[0]), char(rgb[1]), char(rgb[2])};
      out.write(bytes, 3);
    }
  }
  return bool(out);
}

bool renderGl(SoSeparator * scene, std::vector<uint8_t> & pixels,
              SoGLRenderAction::TransparencyType mode)
{
  if (!std::getenv("DISPLAY")) return false;
  SoOffscreenRenderer gl(SbViewportRegion(side, side));
  gl.setComponents(SoOffscreenRenderer::RGB);
  gl.setBackgroundColor(SbColor(0.0f, 0.0f, 0.0f));
  gl.getGLRenderAction()->setTransparencyType(mode);
  if (mode == SoGLRenderAction::SORTED_LAYERS_BLEND)
    gl.getGLRenderAction()->setSortedLayersNumPasses(4);
  if (!gl.render(scene) || !gl.getBuffer()) return false;
  const uint8_t * buffer = gl.getBuffer();
  pixels.assign(buffer, buffer + size_t(side) * side * 3u);
  return true;
}

int renderScene(SoSeparator * scene, std::vector<uint8_t> & bgfxPixels,
                std::vector<uint8_t> & glPixels, bool & glAvailable)
{
  glAvailable = renderGl(scene, glPixels, SoGLRenderAction::SORTED_OBJECT_BLEND);
  SoWgpuSceneManager manager(SbVec2i32(side, side));
  manager.setSceneGraph(scene);
  manager.setBackgroundColor(SbColor4f(0.0f, 0.0f, 0.0f, 1.0f));
  if (!manager.getRenderTarget()->setDepthReadbackEnabled(FALSE)) {
    std::cerr << "Cannot select BGFX RGBA-only target\n";
    return 1;
  }
  const SoWgpuRenderAction::Status status = manager.render();
  if (status == SoWgpuRenderAction::NOT_READY) {
    std::cout << "BGFX renderer unavailable: "
              << manager.getLastError().getString() << '\n';
    return 77;
  }
  if (status != SoWgpuRenderAction::SUCCESS) {
    std::cerr << "BGFX transparency frame failed: "
              << manager.getLastError().getString() << '\n';
    return 1;
  }
  manager.getRenderTarget()->readbackRGBA(bgfxPixels);
  return bgfxPixels.size() == size_t(side) * side * 4u ? 0 : 1;
}

bool near(const Rgb & actual, const Rgb & expected, int tolerance)
{
  for (int channel = 0; channel < 3; ++channel) {
    if (std::abs(actual[channel] - expected[channel]) > tolerance) return false;
  }
  return true;
}

size_t countDifferentPixels(const std::vector<uint8_t> & bgfxPixels,
                            const std::vector<uint8_t> & glPixels, int tolerance)
{
  size_t mismatches = 0;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      if (!near(pixel(bgfxPixels, x, y, 4, false),
                pixel(glPixels, x, y, 3, true), tolerance)) ++mismatches;
    }
  }
  return mismatches;
}

void printSample(const char * label, const Rgb & rgb)
{
  std::cout << label << "=(" << rgb[0] << ',' << rgb[1] << ',' << rgb[2] << ')';
}
}

int main(int argc, char ** argv)
{
  const std::string outputPrefix =
    argc == 3 && std::string(argv[1]) == "--output-prefix" ? argv[2] : "";
  if (argc != 1 && outputPrefix.empty()) {
    std::cerr << "Usage: WgpuBgfxTransparencyTest [--output-prefix path]\n";
    return 2;
  }
  SoDB::init();
  SoWgpuRenderAction::initClass();
  CoinWgpuExperimentalCapabilities caps{};
  if (coin_wgpu_experimental_query_capabilities(
        COIN_WGPU_EXPERIMENTAL_OFFSCREEN, &caps, sizeof(caps)) != 0 ||
      !(caps.features & COIN_WGPU_FEATURE_SORTED_ALPHA)) {
    std::cerr << "BGFX did not advertise sorted alpha composition\n";
    return 1;
  }
  const char * mode = std::getenv("COIN_BGFX_TRANSPARENCY");
  const bool expectLayers = mode && std::string(mode) == "sorted_layers";
  const bool expectWeighted = mode && std::string(mode) == "weighted_oit";
  bool sawGl = false;
  for (int scenario = 0; scenario != 3; ++scenario) {
    const bool crossing = scenario == 1;
    const bool occluded = scenario == 2;
    SoSeparator * scene = makeScene(scenario);
    std::vector<uint8_t> bgfxPixels, glPixels, glLayersPixels;
    bool glAvailable = false;
    const int renderStatus = renderScene(scene, bgfxPixels, glPixels, glAvailable);
    // Coin may fall back to SORTED_OBJECT_BLEND if this GL context lacks
    // the depth/alpha format or extensions required for depth peeling.
    const bool glLayersRendered = crossing && renderStatus == 0 && glAvailable &&
      renderGl(scene, glLayersPixels, SoGLRenderAction::SORTED_LAYERS_BLEND);
    scene->unref();
    if (renderStatus != 0) return renderStatus;
    sawGl = sawGl || glAvailable;
    const char * name = crossing ? "crossing" : (occluded ? "occluded" : "layered");
    const Rgb bgfxLeft = pixel(bgfxPixels, 32, 64, 4, false);
    const Rgb bgfxRight = pixel(bgfxPixels, 96, 64, 4, false);
    std::cout << name << ": ";
    printSample("BGFX left", bgfxLeft);
    std::cout << ' ';
    printSample("right", bgfxRight);
    // Exact peeling follows source-over composition. Weighted OIT is an
    // approximation, so validate its ordering and occlusion invariants.
    if (expectWeighted) {
      for (int y = 20; y < side - 20; ++y) {
        for (int x = 20; x < side - 20; ++x) {
          const Rgb actual = pixel(bgfxPixels, x, y, 4, false);
          const bool opaqueOcclusionFailed = occluded &&
            !near(actual, Rgb{{0, 0, 255}}, 12);
          const bool transparentRangeFailed = !occluded &&
            (actual[0] < 35 || actual[1] < 35 || actual[2] < 45 || actual[2] > 90);
          const bool orderingFailed = !occluded &&
            ((!crossing && actual[0] <= actual[1]) ||
             (crossing && x < 56 && actual[0] <= actual[1]) ||
             (crossing && x > 72 && actual[1] <= actual[0]));
          if (opaqueOcclusionFailed || transparentRangeFailed || orderingFailed) {
            std::cerr << "\nBGFX weighted OIT invariant failed at ("
                      << x << ',' << y << ")\n";
            return 1;
          }
        }
      }
    } else if (!crossing || expectLayers) {
      for (int y = 20; y < side - 20; ++y) {
        for (int x = 20; x < side - 20; ++x) {
          const Rgb expected = occluded ? Rgb{{0, 0, 255}} :
            (crossing && x >= side / 2 ? Rgb{{64, 128, 64}} :
                                        Rgb{{128, 64, 64}});
          if (!near(pixel(bgfxPixels, x, y, 4, false), expected, 12)) {
            std::cerr << "\nBGFX failed analytic composition at ("
                      << x << ',' << y << ")\n";
            return 1;
          }
        }
      }
    }
    if (glAvailable) {
      const Rgb glLeft = pixel(glPixels, 32, 64, 3, true);
      const Rgb glRight = pixel(glPixels, 96, 64, 3, true);
      std::cout << ' ';
      printSample("Coin/GL left", glLeft);
      std::cout << ' ';
      printSample("right", glRight);
      std::cout << " different_pixels="
                << countDifferentPixels(bgfxPixels, glPixels, 2);
    }
    if (glLayersRendered) {
      const Rgb glLeft = pixel(glLayersPixels, 32, 64, 3, true);
      const Rgb glRight = pixel(glLayersPixels, 96, 64, 3, true);
      std::cout << ' ';
      printSample("Coin/GL layers-or-fallback left", glLeft);
      std::cout << ' ';
      printSample("right", glRight);
      std::cout << " different_pixels="
                << countDifferentPixels(bgfxPixels, glLayersPixels, 2);
    }
    std::cout << '\n';
    if (!outputPrefix.empty()) {
      const std::string stem = outputPrefix + "-" + name;
      if (!writePpm(stem + "-bgfx.ppm", bgfxPixels, 4, false) ||
          (glAvailable && !writePpm(stem + "-coin-gl.ppm", glPixels, 3, true)) ||
          (glLayersRendered && !writePpm(stem + "-coin-gl-layers.ppm",
                                         glLayersPixels, 3, true))) {
        std::cerr << "Cannot write comparison PPM files\n";
        return 1;
      }
    }
  }
  if (!sawGl && std::getenv("COIN_WGPU_REQUIRE_GL_REFERENCE")) {
    std::cerr << "Coin/GL reference was requested but unavailable\n";
    return 1;
  }
  if (!sawGl) std::cout << "Coin/GL unavailable; only analytic BGFX checks ran\n";
  return 0;
}
