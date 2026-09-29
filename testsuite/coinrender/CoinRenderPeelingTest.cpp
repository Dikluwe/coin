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
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoEnvironment.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTransparencyCore.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using Color = std::array<float, 4>;
bool check(bool value, const char* message) {
  if (!value)
    std::cerr << "CoinRenderPeelingTest: " << message << '\n';
  return value;
}
Color over(const Color& front, const Color& back) {
  Color result;
  for (int c = 0; c < 3; ++c)
    result[c] = front[c] * front[3] + back[c] * (1 - front[3]);
  result[3] = front[3] + back[3] * (1 - front[3]);
  return result;
}
struct Scene {
  SoSeparator* root = new SoSeparator;
  std::vector<Color> colors;
  std::vector<float> z;
  float referenceDepth = 1;
  Color background = {{.12f, .2f, .3f, 1}};
  Scene(const std::vector<float>& alphas, bool reverse = false, bool singleShape = false,
        float spacing = .25f, bool lighting = false, bool combine = false, float opaqueZ = -3,
        bool opaqueWrite = true, bool withOpaque = true) {
    root->ref();
    auto* camera = new SoOrthographicCamera;
    camera->height = 2;
    camera->position = SbVec3f(0, 0, 10);
    camera->nearDistance = 1;
    camera->farDistance = 30;
    root->addChild(camera);
    auto* model = new SoLightModel;
    model->model = lighting ? SoLightModel::PHONG : SoLightModel::BASE_COLOR;
    root->addChild(model);
    auto* environment = new SoEnvironment;
    environment->ambientIntensity = 0;
    root->addChild(environment);
    if (lighting) {
      auto* light = new SoDirectionalLight;
      light->direction = SbVec3f(0, 0, -1);
      light->color = SbColor(.5f, .75f, 1);
      light->intensity = .5f;
      root->addChild(light);
    }
    for (size_t i = 0; i < alphas.size(); ++i) {
      Color color = {
          {i % 3 == 0 ? .8f : .1f, i % 3 == 1 ? .7f : .2f, i % 3 == 2 ? .9f : .15f, alphas[i]}};
      colors.push_back(color);
      z.push_back(3 - float(i) * spacing);
    }
    auto* group = new SoSeparator;
    if (singleShape)
      root->addChild(group);
    std::vector<SbVec3f> points;
    std::vector<int32_t> indices;
    std::vector<SbColor> diffuse;
    std::vector<float> transparency;
    for (size_t ordinal = 0; ordinal < colors.size(); ++ordinal) {
      const size_t i = reverse ? colors.size() - 1 - ordinal : ordinal;
      if (!singleShape) {
        group = new SoSeparator;
        root->addChild(group);
      }
      if (!singleShape) {
        auto* material = new SoMaterial;
        material->diffuseColor = SbColor(colors[i][0], colors[i][1], colors[i][2]);
        material->transparency = 1 - colors[i][3];
        material->ambientColor = SbColor(0, 0, 0);
        material->specularColor = SbColor(0, 0, 0);
        group->addChild(material);
      }
      diffuse.emplace_back(colors[i][0], colors[i][1], colors[i][2]);
      transparency.push_back(1 - colors[i][3]);
      if (combine) {
        auto* program = new SoTextureCombine;
        program->rgbOperation = SoTextureCombine::REPLACE;
        program->rgbSource.set1Value(0, SoTextureCombine::PREVIOUS);
        program->alphaOperation = SoTextureCombine::REPLACE;
        program->alphaSource.set1Value(0, SoTextureCombine::CONSTANT);
        program->constantColor = SbVec4f(0, 0, 0, colors[i][3]);
        group->addChild(program);
        auto* texture = new SoTexture2;
        const unsigned char pixel[] = {255, 255, 255, 64};
        texture->image.setValue(SbVec2s(1, 1), 4, pixel);
        group->addChild(texture);
        auto* uv = new SoTextureCoordinate2;
        const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(.5f, 1)};
        uv->point.setValues(0, 3, coords);
        group->addChild(uv);
      }
      const SbVec3f triangle[] = {SbVec3f(-.9f, -.9f, z[i]), SbVec3f(.9f, -.9f, z[i]),
                                  SbVec3f(0, .9f, z[i])};
      if (singleShape) {
        const int32_t first = static_cast<int32_t>(points.size());
        points.insert(points.end(), triangle, triangle + 3);
        indices.insert(indices.end(), {first, first + 1, first + 2, -1});
      } else
        geometry(group, triangle, 3, {0, 1, 2, -1});
      if (lighting) {
        colors[i][0] *= .25f;
        colors[i][1] *= .375f;
        colors[i][2] *= .5f;
      }
    }
    if (singleShape) {
      auto* material = new SoMaterial;
      material->diffuseColor.setValues(0, int(diffuse.size()), diffuse.data());
      material->transparency.setValues(0, int(transparency.size()), transparency.data());
      material->ambientColor = SbColor(0, 0, 0);
      material->specularColor = SbColor(0, 0, 0);
      group->addChild(material);
      auto* binding = new SoMaterialBinding;
      binding->value = SoMaterialBinding::PER_FACE;
      group->addChild(binding);
      geometry(group, points.data(), int(points.size()), indices);
    }
    if (!withOpaque) {
      background = {{0, 0, 0, 0}};
      return;
    }
    referenceDepth = opaqueWrite ? (9 - opaqueZ) / 29 : 1;
    for (size_t i = 0; i < colors.size(); ++i)
      if (colors[i][3] == 1)
        referenceDepth = std::min(referenceDepth, (9 - z[i]) / 29);
    // Opaque color is deliberately unlit and can be an occluder without depth writes.
    auto* opaque = new SoSeparator;
    root->addChild(opaque);
    auto* base = new SoLightModel;
    base->model = SoLightModel::BASE_COLOR;
    opaque->addChild(base);
    auto* material = new SoMaterial;
    material->diffuseColor = SbColor(background[0], background[1], background[2]);
    opaque->addChild(material);
    if (!opaqueWrite) {
      auto* depth = new SoDepthBuffer;
      depth->write = FALSE;
      opaque->addChild(depth);
    }
    const SbVec3f triangle[] = {SbVec3f(-.9f, -.9f, opaqueZ), SbVec3f(.9f, -.9f, opaqueZ),
                                SbVec3f(0, .9f, opaqueZ)};
    geometry(opaque, triangle, 3, {0, 1, 2, -1});
  }
  ~Scene() { root->unref(); }
  static void geometry(SoGroup* group, const SbVec3f* points, int count,
                       const std::vector<int32_t>& indices) {
    auto* normal = new SoNormal;
    normal->vector = SbVec3f(0, 0, 1);
    group->addChild(normal);
    auto* binding = new SoNormalBinding;
    binding->value = SoNormalBinding::OVERALL;
    group->addChild(binding);
    auto* coords = new SoCoordinate3;
    coords->point.setValues(0, count, points);
    group->addChild(coords);
    auto* face = new SoIndexedFaceSet;
    face->coordIndex.setValues(0, int(indices.size()), indices.data());
    group->addChild(face);
  }
  Color expected(int layers, int visible = -1) const {
    if (visible < 0)
      visible = int(colors.size());
    Color result = background;
    for (int i = std::min(layers, visible) - 1; i >= 0; --i)
      result = over(colors[i], result);
    return result;
  }
};
struct Runner {
  std::unique_ptr<CoinRenderTarget> target{CoinRenderTarget::createOffscreen(SbVec2i32(64, 64))};
  CoinRenderAction action{SbViewportRegion(64, 64)};
  bool cpu;
  Runner(bool cpu) : cpu(cpu) {
    if (cpu)
      target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
    action.setRenderTarget(target.get());
    action.setTransparencyType(CoinRenderAction::SORTED_LAYERS_BLEND);
  }
  bool render(Scene& scene, int layers, const Color& expected, bool glReference = false,
              int sampleX = 32) {
    action.setSortedLayersNumPasses(layers);
    action.apply(scene.root);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               action.getLastError().getString()))
      return false;
    std::vector<uint8_t> pixels;
    target->readbackRGBA(pixels);
    if (!check(pixels.size() == 64 * 64 * 4, "published RGBA"))
      return false;
    const size_t sample = ((63 - 24) * 64 + sampleX) * 4;
    for (int c = 0; c < 4; ++c) {
      if (std::abs(int(pixels[sample + c]) - int(std::lround(expected[c] * 255))) > 6) {
        std::cerr << "CPU=" << cpu << " layers=" << layers << " channel=" << c
                  << " actual=" << int(pixels[sample + c]) << " expected=" << expected[c] * 255
                  << '\n';
        return false;
      }
    }
    std::vector<float> depth;
    target->readbackDepth(depth);
    if (!check(depth.size() == 64 * 64 &&
                   std::abs(depth[(63 - 24) * 64 + sampleX] - scene.referenceDepth) < .003f,
               "default peeling preserves the actual opaque depth"))
      return false;
    const auto& clear = action.getBackgroundColor();
    for (int c = 0; c < 4; ++c)
      if (!check(std::abs(int(pixels[(2 * 64 + 2) * 4 + c]) - int(std::lround(clear[c] * 255))) <=
                     1,
                 "compositor must not leave halos outside geometry"))
        return false;
    if (glReference && std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE")) {
      SoOffscreenRenderer gl(SbViewportRegion(64, 64));
      gl.setComponents(SoOffscreenRenderer::RGB_TRANSPARENCY);
      gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_LAYERS_BLEND);
      gl.getGLRenderAction()->setSortedLayersNumPasses(layers);
      if (!check(gl.render(scene.root) && gl.getBuffer() &&
                     gl.getGLRenderAction()->getTransparencyType() ==
                         SoGLRenderAction::SORTED_LAYERS_BLEND,
                 "mandatory GL layers without fallback"))
        return false;
      for (int c = 0; c < 3; ++c)
        if (!check(std::abs(int(pixels[sample + c]) -
                            int(gl.getBuffer()[(24 * 64 + sampleX) * 4 + c])) <= 6,
                   "GL many-layer RGB"))
          return false;
    }
    return true;
  }
};
bool matrix(bool cpu) {
  Runner runner(cpu);
  for (int fast : {0, 1}) {
    runner.action.setFastPathEnabled(fast ? TRUE : FALSE);
    for (bool reverse : {false, true})
      for (bool single : {false, true}) {
        Scene scene(std::vector<float>(10, .25f), reverse, single);
        for (int layers : {1, 2, 4, 8, 1})
          if (!runner.render(scene, layers, scene.expected(layers), false))
            return false;
      }
    Scene glLayers(std::vector<float>(6, .25f));
    if (!runner.render(glLayers, 8, glLayers.expected(8), true))
      return false;
    for (float alpha : {0.f, .0001f, 1.f / 255, .5f, .9999f, 1.f}) {
      Scene scene(std::vector<float>(10, alpha));
      if (!runner.render(scene, 8, scene.expected(8)))
        return false;
    }
    Scene zeroChain({0, .5f, .25f});
    if (!runner.render(zeroChain, 1, zeroChain.expected(1)) ||
        !runner.render(zeroChain, 2, zeroChain.expected(2)) ||
        !runner.render(zeroChain, 4, zeroChain.expected(4)))
      return false;
    Scene crossing({.5f, .5f}, false, true);
    auto* group = static_cast<SoGroup*>(crossing.root->getChild(3));
    auto* coordinates = static_cast<SoCoordinate3*>(group->getChild(4));
    const float depths[] = {3, 1, 2, 1, 3, 2};
    for (int i = 0; i < 6; ++i) {
      auto point = coordinates->point[i];
      point[2] = depths[i];
      coordinates->point.set1Value(i, point);
    }
    if (!runner.render(crossing, 4,
                       over(crossing.colors[0], over(crossing.colors[1], crossing.background)),
                       false, 20) ||
        !runner.render(crossing, 4,
                       over(crossing.colors[1], over(crossing.colors[0], crossing.background)),
                       false, 44))
      return false;
    Scene saturation(std::vector<float>(10, .99f));
    for (int i = 0; i < 10; ++i) {
      auto* shape = static_cast<SoGroup*>(saturation.root->getChild(i + 3));
      static_cast<SoMaterial*>(shape->getChild(0))->diffuseColor = SbColor(1, 1, 1);
      saturation.colors[i][0] = saturation.colors[i][1] = saturation.colors[i][2] = 1;
    }
    if (!runner.render(saturation, 8, saturation.expected(8)))
      return false;
    Scene close({.5f, .5f, .5f}, false, false, .0001f);
    if (!runner.render(close, 4, close.expected(4)))
      return false;
    Scene occluded(std::vector<float>(10, .25f), false, false, .25f, false, false, 1.875f);
    if (!runner.render(occluded, 8, occluded.expected(8, 5)))
      return false;
    Scene writing({.5f, .5f}, false, false, .25f, false, false, 5, true);
    if (!runner.render(writing, 8, writing.background))
      return false;
    Scene notWriting({.5f, .5f}, false, false, .25f, false, false, 5, false);
    if (!runner.render(notWriting, 8, notWriting.expected(8)))
      return false;
    for (bool light : {false, true}) {
      Scene textured({.15f, .3f, .5f, .75f, .2f, .4f}, false, false, .25f, light, true);
      if (!runner.render(textured, 8, textured.expected(8)))
        return false;
    }
  }
  runner.action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
  Scene alphaOutput({0, .001f, .25f, .5f, .75f, .2f}, false, false, .25f, false, false, -3, true,
                    false);
  if (!runner.render(alphaOutput, 8, alphaOutput.expected(8)))
    return false;
  runner.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  for (int overrideCase = 0; overrideCase < 3; ++overrideCase) {
    Scene overrides({.5f, .5f});
    if (!runner.render(overrides, 4, overrides.expected(4)))
      return false;
    std::vector<uint8_t> previous;
    runner.target->readbackRGBA(previous);
    const auto previousSerial = runner.target->getLastSubmissionSerial();
    std::vector<SoDepthBuffer*> nodes;
    for (int i = 0; i < 2; ++i) {
      auto* depth = new SoDepthBuffer;
      depth->write = overrideCase == 0;
      depth->test = overrideCase != 1;
      depth->function = overrideCase == 2 ? SoDepthBuffer::GREATER : SoDepthBuffer::LESS;
      static_cast<SoGroup*>(overrides.root->getChild(i + 3))->insertChild(depth, 0);
      nodes.push_back(depth);
    }
#ifdef HAVE_COIN_BGFX
    if (!cpu) {
      runner.action.apply(overrides.root);
      std::vector<uint8_t> preserved;
      runner.target->readbackRGBA(preserved);
      if (!check(runner.action.getLastStatus() == CoinRenderAction::UNSUPPORTED &&
                     preserved == previous &&
                     runner.target->getLastSubmissionSerial() == previousSerial,
                 "unsupported BGFX peeling depth preserves publication"))
        return false;
    } else
#endif
    {
      overrides.referenceDepth = overrideCase == 0 ? 6.f / 29 : 12.f / 29;
      if (!runner.render(overrides, 4,
                         overrideCase == 2 ? overrides.background : overrides.expected(4)))
        return false;
    }
    for (auto* node : nodes) {
      node->write = FALSE;
      node->test = TRUE;
      node->function = SoDepthBuffer::LEQUAL;
    }
    overrides.referenceDepth = 12.f / 29;
    if (!runner.render(overrides, 4, overrides.expected(4)))
      return false;
  }
  Scene valid(std::vector<float>(10, .25f));
  if (!runner.render(valid, 4, valid.expected(4)))
    return false;
  std::vector<uint8_t> before, after;
  runner.target->readbackRGBA(before);
  const auto serial = runner.target->getLastSubmissionSerial();
  for (int invalid : {0, 9, -1}) {
    runner.action.setSortedLayersNumPasses(invalid);
    runner.action.apply(valid.root);
    runner.target->readbackRGBA(after);
    if (!check(runner.action.getLastStatus() == CoinRenderAction::UNSUPPORTED && before == after &&
                   runner.target->getLastSubmissionSerial() == serial &&
                   runner.target->getStatus() == CoinRenderTarget::TARGET_READY,
               "invalid layer count preserves publication"))
      return false;
  }
  runner.action.setSortedLayersNumPasses(4);
  CoinRenderTransparencyOptions options;
  uint64_t required = 0;
  std::string message;
  if (!coin_render_transparency_budget(64, 64, options, true, required, message))
    return false;
  runner.action.setTransparencyBufferBudget(required - 1);
  runner.action.apply(valid.root);
  runner.target->readbackRGBA(after);
  if (!check(runner.action.getLastStatus() == CoinRenderAction::UNSUPPORTED && before == after &&
                 runner.target->getLastSubmissionSerial() == serial,
             "budget rejection preserves frame and serial"))
    return false;
  runner.action.setTransparencyBufferBudget(required);
  return runner.render(valid, 4, valid.expected(4));
}
#ifdef HAVE_COIN_BGFX
Color weightedExpected(const Scene& scene, int visible = -1) {
  if (visible < 0)
    visible = int(scene.colors.size());
  float weights = 0, reveal = 1;
  Color accum = {{0, 0, 0, 0}};
  for (int i = 0; i < visible; ++i) {
    const auto& color = scene.colors[i];
    const float alpha = color[3];
    const float depth = (9 - scene.z[i]) / 29;
    const float weight =
        std::min(8.f, alpha * 8 + .01f) * std::min(16.f, std::pow(1 - depth, 3.f) * 16 + .1f);
    weights += alpha * weight;
    reveal *= 1 - alpha;
    for (int c = 0; c < 3; ++c)
      accum[c] += color[c] * alpha * weight;
  }
  const float opacity = 1 - reveal;
  for (int c = 0; c < 3; ++c)
    accum[c] = weights > 0 ? accum[c] / weights : 0;
  accum[3] = opacity;
  return over(accum, scene.background);
}
bool weightedMatrix() {
  Runner runner(false);
  runner.action.setTransparencyType(CoinRenderAction::SORTED_OBJECT_BLEND);
  for (int fast : {0, 1}) {
    runner.action.setFastPathEnabled(fast ? TRUE : FALSE);
    for (bool reverse : {false, true})
      for (bool single : {false, true}) {
        Scene scene({0, .001f, .01f, .1f, .25f, .5f, .75f, .9f, .99f, .2f}, reverse, single);
        if (!runner.render(scene, 8, weightedExpected(scene)))
          return false;
      }
    for (bool light : {false, true}) {
      Scene texture({.15f, .3f, .5f, .75f, .2f, .4f}, false, false, .25f, light, true);
      if (!runner.render(texture, 8, weightedExpected(texture)))
        return false;
    }
    Scene occluded(std::vector<float>(10, .25f), false, false, .25f, false, false, 1.875f);
    if (!runner.render(occluded, 8, weightedExpected(occluded, 5)))
      return false;
    Scene notWriting({.5f, .5f}, false, false, .25f, false, false, 5, false);
    if (!runner.render(notWriting, 8, weightedExpected(notWriting)))
      return false;
    runner.action.setBackgroundColor(SbColor4f(0, 0, 0, 0));
    Scene alphaOutput({0, .001f, .25f, .5f, .75f, .2f}, false, false, .25f, false, false, -3, true,
                      false);
    if (!runner.render(alphaOutput, 8, weightedExpected(alphaOutput)))
      return false;
    runner.action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  }
  for (bool delayed : {false, true}) {
    Scene mixed({.5f, .5f});
    auto* red = static_cast<SoGroup*>(mixed.root->getChild(3));
    auto* mode = new SoTransparencyType;
    mode->value = delayed ? SoTransparencyType::DELAYED_ADD : SoTransparencyType::ADD;
    red->insertChild(mode, 0);
    if (!delayed) {
      auto* depth = new SoDepthBuffer;
      depth->write = FALSE;
      red->insertChild(depth, 1);
    }
    Color expected = over(mixed.colors[1], mixed.background);
    if (delayed)
      for (int c = 0; c < 3; ++c)
        expected[c] = std::min(1.f, expected[c] + mixed.colors[0][c] * .5f);
    if (!runner.render(mixed, 8, expected))
      return false;
  }
  return true;
}
#endif
bool budgetCore() {
  CoinRenderTransparencyOptions options;
  uint64_t bytes;
  std::string message;
  options.layers = 8;
  options.bufferBudget = ~uint64_t(0);
  return check(!coin_render_transparency_budget(UINT32_MAX, UINT32_MAX, options, true, bytes,
                                                message),
               "budget overflow") &&
         check(coin_render_transparency_budget(64, 64, options, true, bytes, message) &&
                   bytes == 64 * 64 * (8 * 16 + 8),
               "conservative budget accounting");
}
} // namespace
int main(int argc, char** argv) {
  SoDB::init();
  CoinRenderAction::initClass();
  if (argc == 2 && std::string(argv[1]) == "--weighted") {
#ifdef HAVE_COIN_BGFX
    if (!CoinRenderAction::isGpuBackendAvailable())
      return 77;
    if (!weightedMatrix())
      return 1;
    std::cout << "P10 BGFX weighted-OIT numeric matrix passed\n";
    return 0;
#else
    return 77;
#endif
  }
  if (!budgetCore() || !matrix(true))
    return 1;
  if (!CoinRenderAction::isGpuBackendAvailable())
    return 77;
  if (!matrix(false))
    return 1;
  std::cout << "P10 bounded peeling CPU/GPU matrix passed\n";
  return 0;
}
