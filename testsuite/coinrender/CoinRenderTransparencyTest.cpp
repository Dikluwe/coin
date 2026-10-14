#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderComposition.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using A = CoinRenderAction;
bool check(bool value, const char* message) {
  if (!value)
    std::cerr << "CoinRenderTransparencyTest: " << message << '\n';
  return value;
}
const int modes[] = {A::NONE,
                     A::SCREEN_DOOR,
                     A::ADD,
                     A::BLEND,
                     A::DELAYED_ADD,
                     A::DELAYED_BLEND,
                     A::SORTED_OBJECT_ADD,
                     A::SORTED_OBJECT_BLEND,
                     A::SORTED_OBJECT_SORTED_TRIANGLE_ADD,
                     A::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
                     A::SORTED_LAYERS_BLEND};
struct Scene {
  SoSeparator* root = new SoSeparator;
  SoMaterial* nearMaterial = nullptr;
  SoMaterial* farMaterial = nullptr;
  Scene(int topology = 0) {
    root->ref();
    auto* camera = new SoOrthographicCamera;
    camera->height = 2;
    camera->position = SbVec3f(0, 0, 5);
    camera->nearDistance = 1;
    camera->farDistance = 20;
    root->addChild(camera);
    auto* light = new SoLightModel;
    light->model = SoLightModel::BASE_COLOR;
    root->addChild(light);
    nearMaterial = quad(root, 1, SbColor(1, 0, 0), topology);
    farMaterial = quad(root, -1, SbColor(0, 1, 0), topology);
    quad(root, -2, SbColor(0, 0, 1), topology);
  }
  ~Scene() { root->unref(); }
  static SoMaterial* quad(SoGroup* parent, float z, const SbColor& color, int topology = 0) {
    auto* group = new SoSeparator;
    parent->addChild(group);
    auto* material = new SoMaterial;
    material->diffuseColor = color;
    group->addChild(material);
    auto* style = new SoDrawStyle;
    style->lineWidth = 3;
    style->pointSize = 5;
    if (topology == 1)
      style->style = SoDrawStyle::LINES;
    if (topology == 2)
      style->style = SoDrawStyle::POINTS;
    group->addChild(style);
    auto* coordinates = new SoCoordinate3;
    const SbVec3f points[] = {SbVec3f(-.75f, -.75f, z), SbVec3f(.75f, -.75f, z),
                              SbVec3f(.75f, .75f, z), SbVec3f(-.75f, .75f, z)};
    coordinates->point.setValues(0, 4, points);
    group->addChild(coordinates);
    if (topology == 3) {
      auto* line = new SoLineSet;
      line->numVertices = 4;
      group->addChild(line);
    } else if (topology == 4) {
      auto* point = new SoPointSet;
      point->numPoints = 4;
      group->addChild(point);
    } else {
      auto* face = new SoIndexedFaceSet;
      const int32_t indices[] = {0, 1, 2, 3, -1};
      face->coordIndex.setValues(0, 5, indices);
      group->addChild(face);
    }
    return material;
  }
};

struct Runner {
  std::unique_ptr<CoinRenderTarget> target;
  CoinRenderAction action;
  SoOffscreenRenderer gl;
  bool cpu;
  bool compareRGB = true;
  bool reference = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  Runner(bool cpu)
      : target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64))),
        action(SbViewportRegion(64, 64)), gl(SbViewportRegion(64, 64)), cpu(cpu) {
    if (cpu)
      target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
    target->setDepthReadbackEnabled(FALSE);
#endif
    action.setRenderTarget(target.get());
    action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
    gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
    gl.getGLRenderAction()->setSortedLayersNumPasses(4);
  }
  bool render(SoNode* root, int mode, std::vector<uint8_t>& pixels, SoNode* glRoot = nullptr) {
    action.setTransparencyType(static_cast<A::TransparencyType>(mode));
    action.apply(root);
    target->readbackRGBA(pixels);
    if (!check(action.getLastStatus() == A::SUCCESS && pixels.size() == 64 * 64 * 4,
               "mode renders")) {
      std::cerr << "CPU=" << cpu << " mode=" << mode
                << " error=" << action.getLastError().getString() << '\n';
      return false;
    }
    if (reference) {
      gl.getGLRenderAction()->setTransparencyType(
          static_cast<SoGLRenderAction::TransparencyType>(mode));
      if (!check(gl.render(glRoot ? glRoot : root) && gl.getBuffer(), "mandatory Coin GL reference"))
        return false;
      if (!check(gl.getGLRenderAction()->getTransparencyType() ==
                     static_cast<SoGLRenderAction::TransparencyType>(mode),
                 "GL must execute the requested mode without fallback"))
        return false;
    }
    return true;
  }
  bool sample(const std::vector<uint8_t>& pixels, int x, int y,
              const std::array<float, 3>& expected, int mode) {
    for (int c = 0; c < 3; ++c) {
      const int actual = pixels[((63 - y) * 64 + x) * 4 + c];
      if (std::abs(actual - int(std::lround(expected[c] * 255))) > 5) {
        std::cerr << "numeric CPU=" << cpu << " mode=" << mode << " at " << x << ',' << y
                  << " channel=" << c << " expected=" << expected[c] * 255 << " actual=" << actual
                  << '\n';
        return false;
      }
      if (reference && compareRGB &&
          std::abs(actual - int(gl.getBuffer()[(y * 64 + x) * 4 + c])) > 5) {
        std::cerr << "GL CPU=" << cpu << " mode=" << mode << " at " << x << ',' << y
                  << " channel=" << c << " Core=" << actual
                  << " GL=" << int(gl.getBuffer()[(y * 64 + x) * 4 + c]) << '\n';
        return false;
      }
    }
    return true;
  }
};

bool modeMatrix(bool cpu) {
  Runner runner(cpu);
  for (int topology = 0; topology < 5; ++topology) {
    Scene scene(topology);
    for (float alpha : {0.f, .25f, .5f, 1.f})
      for (int fast : {0, 1})
        for (int mode : modes) {
          scene.nearMaterial->transparency = 1 - alpha;
          scene.farMaterial->transparency = 1 - alpha;
          runner.action.setFastPathEnabled(fast ? TRUE : FALSE);
          std::vector<uint8_t> pixels;
          if (!runner.render(scene.root, mode, pixels))
            return false;
          const bool immediate = mode == A::ADD || mode == A::BLEND;
          const bool add = mode == A::ADD || mode == A::DELAYED_ADD ||
                           mode == A::SORTED_OBJECT_ADD ||
                           mode == A::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
          std::array<float, 3> expected;
          if (mode == A::NONE || alpha == 1 || (mode == A::SCREEN_DOOR && topology != 0))
            expected = {{1, 0, 0}};
          else if (mode == A::SCREEN_DOOR) {
            const uint32_t level = uint32_t((1 - alpha) * 64);
            const bool hole = coin_render_screen_door_rank(topology == 2 || topology == 4 ? 8 : 40,
                                                           topology == 0 ? 32 : 8) < level * 16;
            expected = hole ? std::array<float, 3>{{0, 0, 1}} : std::array<float, 3>{{1, 0, 0}};
          } else if (immediate)
            expected = {{alpha, 0, 0}};
          else if (add)
            expected = {{alpha, alpha, 1}};
          else if (mode == A::DELAYED_BLEND)
            expected = {{alpha * (1 - alpha), alpha, (1 - alpha) * (1 - alpha)}};
          else
            expected = {{alpha, alpha * (1 - alpha), (1 - alpha) * (1 - alpha)}};
          const int x = topology == 2 ? 56 : topology == 4 ? 8 : 40, y = topology == 0 ? 32 : 8;
          if (!runner.sample(pixels, x, y, expected, mode))
            return false;
          if (mode == A::SCREEN_DOOR && topology == 0) {
            int red = 0;
            for (int py = 16; py < 48; ++py)
              for (int px = 16; px < 48; ++px) {
                const bool visible =
                    coin_render_screen_door_rank(px, py) >= uint32_t((1 - alpha) * 64) * 16;
                if (!runner.sample(pixels, px, py,
                                   visible ? std::array<float, 3>{{1, 0, 0}}
                                           : std::array<float, 3>{{0, 0, 1}},
                                   mode))
                  return false;
                red += pixels[((63 - py) * 64 + px) * 4] > 128;
              }
            if (!check(red == int(alpha * 1024), "exact 32x32 screen-door coverage"))
              return false;
          }
        }
  }
  return true;
}

bool triangleRuns(bool cpu) {
  Runner runner(cpu);
  Scene scene;
  scene.root->removeChild(3);
  scene.root->removeChild(2);
  auto* shape = new SoSeparator;
  scene.root->addChild(shape);
  auto* material = new SoMaterial;
  const SbColor colors[] = {SbColor(1, 0, 0), SbColor(0, 1, 0)};
  material->diffuseColor.setValues(0, 2, colors);
  material->transparency = .5f;
  shape->addChild(material);
  auto* binding = new SoMaterialBinding;
  binding->value = SoMaterialBinding::PER_FACE;
  shape->addChild(binding);
  auto* coords = new SoCoordinate3;
  const SbVec3f points[] = {SbVec3f(-.75f, -.75f, 1), SbVec3f(.75f, -.75f, 1),
                            SbVec3f(0, .75f, 1),      SbVec3f(-.75f, -.75f, -1),
                            SbVec3f(.75f, -.75f, -1), SbVec3f(0, .75f, -1)};
  coords->point.setValues(0, 6, points);
  shape->addChild(coords);
  auto* face = new SoIndexedFaceSet;
  const int32_t indices[] = {0, 1, 2, -1, 3, 4, 5, -1};
  face->coordIndex.setValues(0, 8, indices);
  shape->addChild(face);
  for (int fast : {0, 1})
    for (int mode : {A::SORTED_OBJECT_BLEND, A::SORTED_OBJECT_SORTED_TRIANGLE_BLEND,
                     A::SORTED_OBJECT_SORTED_TRIANGLE_ADD, A::SORTED_LAYERS_BLEND}) {
      runner.action.setFastPathEnabled(fast ? TRUE : FALSE);
      std::vector<uint8_t> pixels;
      if (!runner.render(scene.root, mode, pixels))
        return false;
      const std::array<float, 3> expected =
          mode == A::SORTED_OBJECT_BLEND                 ? std::array<float, 3>{{.25f, .5f, .25f}}
          : mode == A::SORTED_OBJECT_SORTED_TRIANGLE_ADD ? std::array<float, 3>{{.5f, .5f, 1}}
                                                         : std::array<float, 3>{{.5f, .25f, .25f}};
      if (!runner.sample(pixels, 32, 24, expected, mode))
        return false;
    }
  return true;
}
bool textureAlphaModes(bool cpu) {
  Runner runner(cpu);
  Scene scene;
  Scene glScene;
  // The high-unit contract remains on unit 7 for CPU and GPU. The equivalent
  // GL fixture uses unit 0: legacy fixed-function hardware can expose fewer
  // units than its shader texture-coordinate limit (four vs eight on NVIDIA).
  auto configure = [](Scene& fixture, int activeUnit) {
    fixture.nearMaterial->transparency = .25f;
    fixture.farMaterial->transparency = .25f;
    for (int groupIndex : {2, 3}) {
      auto* group = static_cast<SoGroup*>(fixture.root->getChild(groupIndex));
      auto* unit = new SoTextureUnit;
      unit->unit = activeUnit;
      group->insertChild(unit, 2);
      auto* texture = new SoTexture2;
      const unsigned char pixel[] = {255, 255, 255, 128};
      texture->image.setValue(SbVec2s(1, 1), 4, pixel);
      group->insertChild(texture, 3);
      auto* uv = new SoTextureCoordinate2;
      const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
      uv->point.setValues(0, 4, coords);
      group->insertChild(uv, 4);
      auto* combine = new SoTextureCombine;
      combine->rgbOperation = SoTextureCombine::REPLACE;
      combine->rgbSource.set1Value(0, SoTextureCombine::PREVIOUS);
      combine->alphaOperation = SoTextureCombine::REPLACE;
      combine->alphaSource.set1Value(0, SoTextureCombine::TEXTURE);
      group->insertChild(combine, 3);
      // Coin GL asks for coordinates across the highest active unit, including holes.
      for (int i = 0; i < activeUnit; ++i) {
        auto* lowerUnit = new SoTextureUnit;
        lowerUnit->unit = i;
        group->insertChild(lowerUnit, 2);
        auto* lowerUv = new SoTextureCoordinate2;
        lowerUv->point.setValues(0, 4, coords);
        group->insertChild(lowerUv, 3);
      }
    }
  };
  configure(scene, 7);
  configure(glScene, 0);
  const float alpha = 128.f / 255;
  for (int mode : modes) {
    // Coin's ARB peel program handles texture[0] only and replaces combine.
    // Keep GL execution mandatory, but qualify this extension against numeric RGBA.
    runner.compareRGB = mode != A::SORTED_LAYERS_BLEND;
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, mode, pixels, glScene.root))
      return false;
    std::array<float, 3> expected;
    if (mode == A::NONE)
      expected = {{1, 0, 0}};
    else if (mode == A::SCREEN_DOOR)
      expected = coin_render_screen_door_rank(40, 32) >= 256 ? std::array<float, 3>{{1, 0, 0}}
                                                             : std::array<float, 3>{{0, 0, 1}};
    else if (mode == A::ADD || mode == A::BLEND)
      expected = {{alpha, 0, 0}};
    else if (mode == A::DELAYED_ADD || mode == A::SORTED_OBJECT_ADD ||
             mode == A::SORTED_OBJECT_SORTED_TRIANGLE_ADD)
      expected = {{alpha, alpha, 1}};
    else if (mode == A::DELAYED_BLEND)
      expected = {{alpha * (1 - alpha), alpha, (1 - alpha) * (1 - alpha)}};
    else
      expected = {{alpha, alpha * (1 - alpha), (1 - alpha) * (1 - alpha)}};
    if (!runner.sample(pixels, 40, 32, expected, mode))
      return false;
  }
  return true;
}

bool annotationModes(bool cpu) {
  Runner runner(cpu);
  Scene scene;
  scene.root->removeChild(3);
  scene.root->removeChild(2);
  auto* annotation = new SoAnnotation;
  scene.root->addChild(annotation);
  // Opaque depth writer, then translucent foreground in traversal order.
  Scene::quad(annotation, -1, SbColor(0, 1, 0));
  auto* red = Scene::quad(annotation, 1, SbColor(1, 0, 0));
  red->transparency = .5f;
  for (int mode : modes) {
    // Coin GL sorted-layers skips this delayed annotation path.
    runner.compareRGB = mode != A::SORTED_LAYERS_BLEND;
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, mode, pixels))
      return false;
    std::array<float, 3> expected;
    if (mode == A::NONE)
      expected = {{1, 0, 0}};
    else if (mode == A::SCREEN_DOOR)
      expected = coin_render_screen_door_rank(40, 32) >= 512 ? std::array<float, 3>{{1, 0, 0}}
                                                             : std::array<float, 3>{{0, 1, 0}};
    else if (mode == A::ADD || mode == A::DELAYED_ADD || mode == A::SORTED_OBJECT_ADD ||
             mode == A::SORTED_OBJECT_SORTED_TRIANGLE_ADD)
      expected = {{.5f, 1, 0}};
    else
      expected = {{.5f, .5f, 0}};
    if (!runner.sample(pixels, 40, 32, expected, mode))
      return false;
  }
  return true;
}

bool explicitDepthModes(bool cpu) {
  Runner runner(cpu);
  Scene scene;
  scene.nearMaterial->transparency = .5f;
  scene.farMaterial->transparency = .5f;
  SoDepthBuffer* depth[2];
  for (int i = 0; i < 2; ++i) {
    depth[i] = new SoDepthBuffer;
    depth[i]->function = SoDepthBuffer::NEVER;
    depth[i]->write = FALSE;
    static_cast<SoGroup*>(scene.root->getChild(i + 2))->insertChild(depth[i], 2);
  }
  for (int mode : modes) {
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, mode, pixels) ||
        !runner.sample(pixels, 40, 32, {{0, 0, 1}}, mode))
      return false;
  }
  for (auto* node : depth) {
    node->function = SoDepthBuffer::LESS;
    node->write = FALSE;
  }
  for (int mode : {A::ADD, A::BLEND}) {
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, mode, pixels) ||
        !runner.sample(pixels, 40, 32, {{0, 0, 1}}, mode))
      return false;
  }
  return true;
}

bool crossingLayers(bool cpu) {
  Runner runner(cpu);
  Scene scene;
  scene.nearMaterial->transparency = .5f;
  scene.farMaterial->transparency = .5f;
  for (int groupIndex : {2, 3}) {
    auto* group = static_cast<SoGroup*>(scene.root->getChild(groupIndex));
    auto* coords = static_cast<SoCoordinate3*>(group->getChild(2));
    const float sign = groupIndex == 2 ? 1.f : -1.f;
    for (int i = 0; i < 4; ++i) {
      SbVec3f p = coords->point[i];
      p[2] = p[0] < 0 ? sign : -sign;
      coords->point.set1Value(i, p);
    }
  }
  for (int fast : {0, 1}) {
    runner.action.setFastPathEnabled(fast ? TRUE : FALSE);
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, A::SORTED_LAYERS_BLEND, pixels) ||
        !runner.sample(pixels, 20, 32, {{.5f, .25f, .25f}}, A::SORTED_LAYERS_BLEND) ||
        !runner.sample(pixels, 44, 32, {{.25f, .5f, .25f}}, A::SORTED_LAYERS_BLEND))
      return false;
  }
  return true;
}

bool outputAlpha(bool cpu) {
  Runner runner(cpu);
  runner.reference = false; // CoinRender defines separate source-over alpha.
  runner.action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  Scene scene;
  scene.root->removeChild(4);
  scene.root->removeChild(3);
  scene.nearMaterial->transparency = .5f;
  for (int mode : modes) {
    std::vector<uint8_t> pixels;
    if (!runner.render(scene.root, mode, pixels))
      return false;
    const bool add = mode == A::ADD || mode == A::DELAYED_ADD || mode == A::SORTED_OBJECT_ADD ||
                     mode == A::SORTED_OBJECT_SORTED_TRIANGLE_ADD;
    const float expected = mode == A::SCREEN_DOOR
                               ? (coin_render_screen_door_rank(40, 32) >= 512 ? 1.f : 0.f)
                           : add ? .25f
                                 : .5f;
    const int actual = pixels[((63 - 32) * 64 + 40) * 4 + 3];
    if (std::abs(actual - int(std::lround(expected * 255))) > 5) {
      std::cerr << "output alpha CPU=" << cpu << " mode=" << mode << " actual=" << actual << '\n';
      return false;
    }
  }
  return true;
}
} // namespace
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  std::cout << "CPU transparency matrices" << std::endl;
  if (!modeMatrix(true) || !triangleRuns(true) || !crossingLayers(true) || !outputAlpha(true) ||
      !check(textureAlphaModes(true), "CPU texture alpha matrix") ||
      !check(annotationModes(true), "CPU annotation matrix") ||
      !check(explicitDepthModes(true), "CPU explicit depth matrix"))
    return 1;
  if (!CoinRenderAction::isGpuBackendAvailable())
    return 77;
  if (!modeMatrix(false) || !triangleRuns(false) || !crossingLayers(false) || !outputAlpha(false) ||
      !check(textureAlphaModes(false), "GPU texture alpha matrix") ||
      !check(annotationModes(false), "GPU annotation matrix") ||
      !check(explicitDepthModes(false), "GPU explicit depth matrix"))
    return 1;
  std::cout << "P09 eleven-mode action matrix passed, CPU and GPU\n";
  return 0;
}
