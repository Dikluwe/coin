#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SoOffscreenRenderer.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/details/SoLineDetail.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTextureCombine.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include "rendering/coinrender/CoinRenderTextureCombineCore.h"
#include "rendering/coinrender/CoinRenderTargetP.h"
#include "rendering/coinrender/CoinRenderCpuReferenceBackend.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
bool check(bool condition, const char* message) {
  if (!condition)
    std::cerr << "CoinRenderMultitextureTest: " << message << '\n';
  return condition;
}
struct Scene {
  SoSeparator* root = new SoSeparator;
  SoTexture2* textures[8];
  SoTexture2Transform* matrices[8];
  SoTextureCoordinate2* uv[8];
  SoSwitch* units[8];
  SoTextureCombine* combine = new SoTextureCombine;
  SoSwitch* combineSwitch = new SoSwitch;
  SoDrawStyle* style = new SoDrawStyle;
  SoGroup* geometry = new SoGroup;
  SoMaterial* material = new SoMaterial;
  SoCoordinate3* coordinates = new SoCoordinate3;
  Scene() {
    root->ref();
    auto* camera = new SoOrthographicCamera;
    camera->height = 2;
    camera->position = SbVec3f(0, 0, 3);
    camera->nearDistance = .1f;
    camera->farDistance = 10;
    root->addChild(camera);
    auto* lighting = new SoLightModel;
    lighting->model = SoLightModel::BASE_COLOR;
    root->addChild(lighting);
    material->diffuseColor = SbColor(.4f, .5f, .6f);
    material->transparency = .25f;
    root->addChild(material);
    for (int i = 0; i < 8; ++i) {
      units[i] = new SoSwitch;
      units[i]->whichChild = i < 2 ? SO_SWITCH_ALL : SO_SWITCH_NONE;
      root->addChild(units[i]);
      auto* unit = new SoTextureUnit;
      unit->unit = i;
      units[i]->addChild(unit);
      uv[i] = new SoTextureCoordinate2;
      const SbVec2f coords[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
      uv[i]->point.setValues(0, 4, coords);
      units[i]->addChild(uv[i]);
      matrices[i] = new SoTexture2Transform;
      units[i]->addChild(matrices[i]);
      textures[i] = new SoTexture2;
      textures[i]->model = SoTexture2::MODULATE;
      unsigned char pixel[] = {255, 255, 255, 255};
      if (i == 0) {
        pixel[0] = 51;
        pixel[1] = 102;
        pixel[2] = 153;
      }
      if (i == 1) {
        pixel[0] = 102;
        pixel[1] = 153;
        pixel[2] = 204;
        pixel[3] = 192;
      }
      if (i == 1) {
        combineSwitch->whichChild = SO_SWITCH_ALL;
        combineSwitch->addChild(combine);
        units[i]->addChild(combineSwitch);
      }
      textures[i]->image.setValue(SbVec2s(1, 1), 4, pixel);
      units[i]->addChild(textures[i]);
    }
    auto* unit = new SoTextureUnit;
    unit->unit = 0;
    root->addChild(unit);
    root->addChild(style);
    const SbVec3f points[] = {SbVec3f(-.75f, -.75f, 0), SbVec3f(.75f, -.75f, 0),
                              SbVec3f(.75f, .75f, 0), SbVec3f(-.75f, .75f, 0)};
    coordinates->point.setValues(0, 4, points);
    root->addChild(coordinates);
    root->addChild(geometry);
    combine->constantColor = SbVec4f(.3f, .4f, .6f, .8f);
    const int source[] = {SoTextureCombine::PREVIOUS, SoTextureCombine::TEXTURE,
                          SoTextureCombine::CONSTANT};
    combine->rgbSource.setValues(0, 3, source);
    combine->alphaSource.setValues(0, 3, source);
    shape(0);
  }
  ~Scene() { root->unref(); }
  void shape(int kind) {
    geometry->removeAllChildren();
    style->style = kind == 1   ? SoDrawStyle::LINES
                   : kind == 2 ? SoDrawStyle::POINTS
                               : SoDrawStyle::FILLED;
    style->lineWidth = 3;
    style->pointSize = 5;
    if (kind < 3) {
      auto* f = new SoIndexedFaceSet;
      const int32_t ix[] = {0, 1, 2, 3, -1};
      f->coordIndex.setValues(0, 5, ix);
      geometry->addChild(f);
    } else if (kind == 3) {
      auto* l = new SoLineSet;
      l->numVertices = 4;
      geometry->addChild(l);
    } else {
      auto* p = new SoPointSet;
      p->numPoints = 4;
      geometry->addChild(p);
    }
  }
};

bool lineDetailContract() {
  Scene scene;
  scene.shape(3);
  struct Result {
    int segment = 0;
    bool valid = true;
  } result;
  SoCallbackAction callback;
  callback.addLineSegmentCallback(
      SoLineSet::getClassTypeId(),
      [](void* data, SoCallbackAction*, const SoPrimitiveVertex* a, const SoPrimitiveVertex* b) {
        auto& r = *static_cast<Result*>(data);
        const auto* detail = static_cast<const SoLineDetail*>(a->getDetail());
        r.valid = r.valid && detail && detail->getPoint0()->getCoordinateIndex() == r.segment &&
                  detail->getPoint1()->getCoordinateIndex() == r.segment + 1 &&
                  detail->getPoint0()->getTextureCoordIndex() == r.segment &&
                  detail->getPoint1()->getTextureCoordIndex() == r.segment + 1;
        if (!r.valid)
          std::cerr << "Line detail segment " << r.segment << '\n';
        ++r.segment;
        (void)b;
      },
      &result);
  callback.apply(scene.root);
  return check(result.valid && result.segment == 3,
               "Coin LINE_STRIP preserves endpoint coordinate/UV details");
}

bool rasterContract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  action.setTransparencyType(CoinRenderAction::BLEND);
  SoOffscreenRenderer gl(SbViewportRegion(64, 64));
  gl.setComponents(SoOffscreenRenderer::RGB);
  gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  const bool compare = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  Scene scene;
  for (int i = 0; i < 8; ++i)
    scene.units[i]->whichChild = SO_SWITCH_NONE;
  scene.material->diffuseColor = SbColor(1, 1, 1);
  scene.material->transparency = .5f;
  scene.geometry->removeAllChildren();
  auto* line = new SoLineSet;
  line->numVertices = 2;
  scene.geometry->addChild(line);
  const float cases[][4] = {{6.5f, 6.5f, 45.5f, 6.5f},
                            {6.5f, 6.5f, 6.5f, 45.5f},
                            {6.5f, 6.5f, 45.5f, 45.5f},
                            {6.25f, 6.75f, 45.25f, 26.75f},
                            {6.75f, 6.25f, 26.75f, 45.25f},
                            {45.5f, 31.25f, 6.5f, 6.25f},
                            {6, 6.5f, 45, 45.5f}};
  for (int caseIndex = 0; caseIndex < 7; ++caseIndex)
    for (float width : {.75f, 1.f, 2.f, 3.f, 4.f, 6.6f})
      for (uint32_t pattern : {0u, 0xffffu, 0x000fu, 0xaaaau}) {
        const auto& c = cases[caseIndex];
        scene.coordinates->point.set1Value(0, SbVec3f(c[0] / 32 - 1, c[1] / 32 - 1, 0));
        scene.coordinates->point.set1Value(1, SbVec3f(c[2] / 32 - 1, c[3] / 32 - 1, 0));
        scene.style->lineWidth = width;
        scene.style->linePattern = pattern;
        action.apply(scene.root);
        std::vector<uint8_t> image;
        target->readbackRGBA(image);
        if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                   "fractional/diagonal stroke raster"))
          return false;
        std::vector<uint8_t> ref;
        if (compare) {
          if (!check(gl.render(scene.root) && gl.getBuffer(), "mandatory GL stroke raster"))
            return false;
          ref.assign(gl.getBuffer(), gl.getBuffer() + 64 * 64 * 3);
        }
        int actualCount = 0, referenceCount = 0;
        auto actual = [&](int x, int y) { return image[((63 - y) * 64 + x) * 4] > 40; };
        auto reference = [&](int x, int y) { return ref[(y * 64 + x) * 3] > 40; };
        auto nearby = [&](int x, int y, bool useReference) {
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              if (x + dx >= 0 && x + dx < 64 && y + dy >= 0 && y + dy < 64 &&
                  (useReference ? reference(x + dx, y + dy) : actual(x + dx, y + dy)))
                return true;
          return false;
        };
        for (int y = 0; y < 64; ++y)
          for (int x = 0; x < 64; ++x) {
            if (actual(x, y)) {
              ++actualCount;
              if (!check(std::abs(int(image[((63 - y) * 64 + x) * 4]) - 128) <= 1,
                         "one source-over contribution per isolated line fragment"))
                return false;
              if (compare && !nearby(x, y, true)) {
                std::cerr << "raster Core-only case " << caseIndex << " width " << width
                          << " pattern " << pattern << " at " << x << ',' << y << '\n';
                return false;
              }
            }
            if (compare && reference(x, y)) {
              ++referenceCount;
              if (!nearby(x, y, false)) {
                std::cerr << "raster GL-only case " << caseIndex << " width " << width
                          << " pattern " << pattern << " at " << x << ',' << y << '\n';
                return false;
              }
            }
          }
        const int rounded = std::max(1, int(std::floor(width + .5f)));
        if (compare &&
            !check(std::abs(actualCount - referenceCount) <= rounded,
                   "aliased line count differs by at most one replicated unit fragment")) {
          std::cerr << "raster counts case " << caseIndex << " width " << width << " pattern "
                    << pattern << " Core " << actualCount << " GL " << referenceCount << '\n';
          return false;
        }
        if (pattern == 0 && !check(actualCount == 0, "zero line mask leaves clear"))
          return false;
      }
  // Aliased point centers snap differently for odd and even rounded sizes.
  scene.geometry->removeAllChildren();
  auto* point = new SoPointSet;
  point->numPoints = 1;
  scene.geometry->addChild(point);
  for (float coordinate : {20.f, 20.25f, 20.5f, 20.75f})
    for (float size : {.75f, 1.f, 2.f, 3.f, 4.f, 6.6f}) {
      scene.coordinates->point.set1Value(0, SbVec3f(coordinate / 32 - 1, coordinate / 32 - 1, 0));
      scene.style->pointSize = size;
      action.apply(scene.root);
      std::vector<uint8_t> image;
      target->readbackRGBA(image);
      if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS, "fractional point raster"))
        return false;
      const int rounded = std::max(1, int(std::floor(size + .5f)));
      const float center =
          rounded % 2 ? std::floor(coordinate) + .5f : std::floor(coordinate + .5f);
      int count = 0;
      if (compare && !check(gl.render(scene.root) && gl.getBuffer(), "mandatory GL point raster"))
        return false;
      for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
          const bool expected =
              x + .5f >= center - rounded * .5f && x + .5f < center + rounded * .5f &&
              y + .5f >= center - rounded * .5f && y + .5f < center + rounded * .5f;
          const int value = image[((63 - y) * 64 + x) * 4];
          count += value > 40;
          if (!check(std::abs(value - (expected ? 128 : 0)) <= 1,
                     "point has exact rounded square and single alpha coverage"))
            return false;
          if (compare && !check(std::abs(int(gl.getBuffer()[(y * 64 + x) * 3]) - value) <= 1,
                                "aliased point pixels agree with GL"))
            return false;
        }
      if (!check(count == rounded * rounded, "point pixel count equals rounded size squared"))
        return false;
    }
  // Native clipping has a deterministic visible-fragment counter in Core;
  // GL deliberately leaves the initial phase of a clipped line unspecified.
  scene.geometry->removeAllChildren();
  line = new SoLineSet;
  line->numVertices = 2;
  scene.geometry->addChild(line);
  scene.coordinates->point.set1Value(0, SbVec3f(6.5f / 32 - 1, 6.5f / 32 - 1, 0));
  scene.coordinates->point.set1Value(1, SbVec3f(45.5f / 32 - 1, 6.5f / 32 - 1, 0));
  auto* clip = new SoClipPlane;
  clip->plane = SbPlane(SbVec3f(1, 0, 0), 16.5f / 32 - 1);
  scene.root->insertChild(clip, scene.root->getNumChildren() - 1);
  for (int repeat : {1, 2, 5})
    for (uint32_t pattern : {0u, 0xffffu, 0x000fu, 0xaaaau}) {
      scene.style->lineWidth = 3;
      scene.style->linePattern = pattern;
      scene.style->linePatternScaleFactor = repeat;
      action.apply(scene.root);
      std::vector<uint8_t> image;
      target->readbackRGBA(image);
      if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
                 "clipped patterned native line"))
        return false;
      for (int x = 20; x < 40; ++x) {
        const bool expected = (pattern & (1u << (((x - 16) / repeat) & 15))) != 0;
        if (!check(std::abs(int(image[((63 - 6) * 64 + x) * 4]) - (expected ? 128 : 0)) <= 1,
                   "clipped line uses visible fragment phase and alpha"))
          return false;
      }
    }
  clip->plane = SbPlane(SbVec3f(1, 0, 0), 2);
  const uint64_t before = target->getLastSubmissionSerial();
  action.apply(scene.root);
  std::vector<uint8_t> clear;
  target->readbackRGBA(clear);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 before < target->getLastSubmissionSerial(),
             "fully clipped native line publishes clear"))
    return false;
  for (size_t i = 0; i < clear.size(); i += 4)
    if (!check(clear[i] == 0, "fully clipped native line has no stale pixels"))
      return false;
  clip->on = FALSE;
  action.apply(scene.root);
  return check(action.getLastStatus() == CoinRenderAction::SUCCESS,
               "native strokes recover after fully clipped frame");
}

bool contract(bool cpu) {
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(64, 64)));
  if (cpu)
    target->getPimpl()->backend.reset(new CoinRenderCpuReferenceBackend);
#ifdef HAVE_COIN_BGFX
  target->setDepthReadbackEnabled(FALSE);
#endif
  CoinRenderAction action(SbViewportRegion(64, 64));
  action.setRenderTarget(target.get());
  action.setTransparencyType(CoinRenderAction::BLEND);
  action.setBackgroundColor(SbColor4f(0, 0, 0, 1));
  const bool referenceRequired = std::getenv("COIN_RENDER_REQUIRE_GL_REFERENCE") != nullptr;
  SoOffscreenRenderer gl(SbViewportRegion(64, 64));
  gl.setComponents(SoOffscreenRenderer::RGB);
  gl.getGLRenderAction()->setTransparencyType(SoGLRenderAction::BLEND);
  Scene scene;
  const int operations[] = {SoTextureCombine::REPLACE,  SoTextureCombine::MODULATE,
                            SoTextureCombine::ADD,      SoTextureCombine::ADD_SIGNED,
                            SoTextureCombine::SUBTRACT, SoTextureCombine::INTERPOLATE,
                            SoTextureCombine::DOT3_RGB, SoTextureCombine::DOT3_RGBA};
  // Independent numeric RGB table: previous=(.08,.20,.36), texture=(.4,.6,.8).
  // Dot uses PRIMARY_COLOR=(.4,.5,.6) instead of previous, producing .16.
  const float rgb[][3] = {{.08f, .20f, .36f}, {.032f, .12f, .288f}, {.48f, .8f, 1.16f},
                          {-.02f, .3f, .66f}, {-.32f, -.4f, -.44f}, {.304f, .44f, .536f},
                          {.16f, .16f, .16f}, {.16f, .16f, .16f}};
  auto verify = [&](int x, int y, const float expected[4], const char* message) {
    action.apply(scene.root);
    std::vector<uint8_t> image;
    target->readbackRGBA(image);
    if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS && image.size() == 64 * 64 * 4,
               message)) {
      std::cerr << "status " << action.getLastStatus() << " " << action.getLastError().getString()
                << '\n';
      return false;
    }
    const size_t at = ((63 - y) * 64 + x) * 4;
    for (int c = 0; c < 3; ++c)
      if (std::abs(int(image[at + c]) - int(std::lround(expected[c] * expected[3] * 255))) > 3) {
        std::cerr << "P08 " << cpu << " at " << x << ',' << y << " channel " << c << " expected "
                  << expected[c] * expected[3] * 255 << " got " << int(image[at + c]) << '\n';
        return false;
      }
    if (!check(image[at + 3] == 255, "source-over alpha preserves opaque clear"))
      return false;
    if (referenceRequired) {
      if (!check(gl.render(scene.root) && gl.getBuffer(),
                 "mandatory Coin GL multitexture reference"))
        return false;
      const unsigned char* ref = gl.getBuffer();
      for (int c = 0; c < 3; ++c)
        if (std::abs(int(ref[(y * 64 + x) * 3 + c]) - int(image[at + c])) > 4) {
          std::cerr << "P08 GL " << cpu << " op " << scene.combine->rgbOperation.getValue()
                    << " scales " << scene.combine->rgbScale.getValue() << ','
                    << scene.combine->alphaScale.getValue() << " channel " << c << " GL "
                    << int(ref[(y * 64 + x) * 3 + c]) << " Core " << int(image[at + c]) << '\n';
          return false;
        }
    }
    return true;
  };
  for (int kind = 0; kind < 5; ++kind)
    for (int fast : {0, 1})
      for (int op = 0; op < 8; ++op)
        for (int scale : {1, 2, 4}) {
          scene.shape(kind);
          action.setFastPathEnabled(fast ? TRUE : FALSE);
          scene.combine->rgbOperation = operations[op];
          scene.combine->alphaOperation = SoTextureCombine::MODULATE;
          scene.combine->rgbScale = scale;
          scene.combine->alphaScale = 1;
          scene.combine->rgbSource.set1Value(0, op >= 6 ? SoTextureCombine::PRIMARY_COLOR
                                                        : SoTextureCombine::PREVIOUS);
          float expected[4];
          for (int c = 0; c < 3; ++c)
            expected[c] = std::max(0.f, std::min(1.f, rgb[op][c] * scale));
          expected[3] = op == 7 ? .16f : .75f * 192 / 255;
          const int x = kind == 2 || kind == 4 ? 8 : 32, y = kind == 0 ? 32 : 8;
          if (!verify(x, y, expected, "combine operation on textured polygon/line/point"))
            return false;
        }
  scene.shape(0);
  scene.combine->rgbOperation = SoTextureCombine::DOT3_RGBA;
  scene.combine->alphaOperation = SoTextureCombine::SUBTRACT;
  for (int rgbScale : {1, 2, 4})
    for (int alphaScale : {1, 2, 4}) {
      scene.combine->rgbScale = rgbScale;
      scene.combine->alphaScale = alphaScale;
      const float expected[] = {.16f * rgbScale, .16f * rgbScale, .16f * rgbScale,
                                .16f * alphaScale};
      if (!verify(32, 32, expected,
                  "DOT3_RGBA ignores alpha operation but keeps independent scale"))
        return false;
    }
  scene.shape(0);
  scene.combine->rgbOperation = SoTextureCombine::REPLACE;
  scene.combine->rgbSource.set1Value(0, SoTextureCombine::TEXTURE);
  scene.combine->rgbScale = 1;
  const float alpha[] = {.75f,
                         .75f * 192 / 255,
                         1,
                         .75f + 192.f / 255 - .5f,
                         .75f - 192.f / 255,
                         .75f * .8f + (192.f / 255) * .2f};
  for (int op = 0; op < 6; ++op)
    for (int scale : {1, 2, 4}) {
      scene.combine->alphaOperation = operations[op];
      scene.combine->alphaScale = scale;
      const float expected[] = {.4f, .6f, .8f, std::max(0.f, std::min(1.f, alpha[op] * scale))};
      if (!verify(32, 32, expected, "independent alpha operation and scale"))
        return false;
    }
  // All source selectors and RGB operands. REPLACE isolates each argument.
  scene.combine->alphaOperation = SoTextureCombine::REPLACE;
  scene.combine->alphaScale = 1;
  scene.combine->alphaSource.set1Value(0, SoTextureCombine::CONSTANT);
  const int sources[] = {SoTextureCombine::PRIMARY_COLOR, SoTextureCombine::TEXTURE,
                         SoTextureCombine::CONSTANT, SoTextureCombine::PREVIOUS};
  const int operands[] = {SoTextureCombine::SRC_COLOR, SoTextureCombine::ONE_MINUS_SRC_COLOR,
                          SoTextureCombine::SRC_ALPHA, SoTextureCombine::ONE_MINUS_SRC_ALPHA};
  const float colors[][4] = {{.4f, .5f, .6f, .75f},
                             {.4f, .6f, .8f, 192.f / 255},
                             {.3f, .4f, .6f, .8f},
                             {.08f, .2f, .36f, .75f}};
  for (int src = 0; src < 4; ++src)
    for (int operand = 0; operand < 4; ++operand) {
      scene.combine->rgbSource.set1Value(0, sources[src]);
      scene.combine->rgbOperand.set1Value(0, operands[operand]);
      float expected[4] = {0, 0, 0, .8f};
      for (int c = 0; c < 3; ++c) {
        expected[c] = colors[src][operand >= 2 ? 3 : c];
        if (operand == 1 || operand == 3)
          expected[c] = 1 - expected[c];
      }
      if (!verify(32, 32, expected, "combine source and operand"))
        return false;
    }
  scene.combine->rgbOperand.set1Value(0, SoTextureCombine::SRC_COLOR);
  scene.combine->rgbSource.set1Value(0, SoTextureCombine::TEXTURE);
  for (int src = 0; src < 4; ++src)
    for (int complement = 0; complement < 2; ++complement) {
      scene.combine->alphaSource.set1Value(0, sources[src]);
      scene.combine->alphaOperand.set1Value(0, complement ? SoTextureCombine::ONE_MINUS_SRC_ALPHA
                                                          : SoTextureCombine::SRC_ALPHA);
      const float expected[] = {.4f, .6f, .8f, complement ? 1 - colors[src][3] : colors[src][3]};
      if (!verify(32, 32, expected, "independent alpha source and complement"))
        return false;
    }
  // Eight units and sparse unit seven, with a unique last-unit color.
  scene.combineSwitch->whichChild = SO_SWITCH_NONE;
  scene.material->transparency = 0;
  const unsigned char last[] = {40, 180, 220, 255};
  scene.textures[7]->image.setValue(SbVec2s(1, 1), 4, last);
  scene.textures[7]->model = SoTexture2::REPLACE;
  const float lastExpected[] = {40.f / 255, 180.f / 255, 220.f / 255, 1};
  for (int sparse : {0, 1})
    for (int kind = 0; kind < 5; ++kind) {
      scene.shape(kind);
      for (int i = 0; i < 8; ++i)
        scene.units[i]->whichChild = !sparse || i == 7 ? SO_SWITCH_ALL : SO_SWITCH_NONE;
      if (!verify(kind == 2 || kind == 4 ? 8 : 32, kind == 0 ? 32 : 8, lastExpected,
                  "eight texture units including sparse last unit"))
        return false;
    }
  // Distinct UV/matrix/sampler state in every slot, including unit zero disabled.
  // A 2x2 checker makes missing UV transport or a wrong slot observable.
  const unsigned char checker[] = {255, 0, 0,   255, 0,   255, 0, 255,
                                   0,   0, 255, 255, 255, 255, 0, 255};
  for (int unit = 0; unit < 8; ++unit) {
    for (int i = 0; i < 8; ++i)
      scene.units[i]->whichChild = i == unit ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    scene.textures[unit]->model = SoTexture2::REPLACE;
    scene.textures[unit]->wrapS = SoTexture2::CLAMP;
    scene.textures[unit]->wrapT = SoTexture2::CLAMP;
    scene.textures[unit]->image.setValue(SbVec2s(2, 2), 4, checker);
    for (int i = 0; i < 4; ++i)
      scene.uv[unit]->point.set1Value(i, SbVec2f(.25f, .25f));
    for (int shift = 0; shift < 2; ++shift)
      for (int kind = 0; kind < 5; ++kind) {
        scene.shape(kind);
        scene.matrices[unit]->translation = SbVec2f(shift * .5f, 0);
        const float expected[] = {shift ? 0.f : 1.f, shift ? 1.f : 0.f, 0, 1};
        if (!verify(kind == 2 || kind == 4 ? 8 : 32, kind == 0 ? 32 : 8, expected,
                    "per-unit UV and matrix on sparse textured strokes"))
          return false;
      }
    scene.matrices[unit]->translation = SbVec2f(0, 0);
    const SbVec2f varying[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
    scene.uv[unit]->point.setValues(0, 4, varying);
    scene.shape(0);
    const float expected[] = {0, 1, 0, 1};
    // x=48,y=16 samples the clamped green texel, independently of the triangulation diagonal.
    if (!verify(48, 16, expected, "varying UV in a sparse unit"))
      return false;
    scene.shape(3);
    if (!verify(48, 8, expected, "varying UV in a sparse native line"))
      return false;
    if (!verify(56, 16, expected, "advanced LINE_STRIP endpoint UV indices"))
      return false;
    const float blue[] = {0, 0, 1, 1};
    if (!verify(16, 56, blue, "third segment UV after repeated strip advancement"))
      return false;
    scene.shape(4);
    const float red[] = {1, 0, 0, 1};
    if (!verify(8, 8, red, "point keeps its own sparse-unit UV"))
      return false;
  }
  // Reject malformed combine without publishing; separator restores valid state.
  for (int i = 0; i < 8; ++i)
    scene.units[i]->whichChild = i < 2 ? SO_SWITCH_ALL : SO_SWITCH_NONE;
  scene.shape(0);
  scene.combineSwitch->whichChild = SO_SWITCH_ALL;
  scene.combine->rgbScale = 3;
  std::vector<uint8_t> before, after;
  target->readbackRGBA(before);
  const uint64_t serial = target->getLastSubmissionSerial();
  action.apply(scene.root);
  target->readbackRGBA(after);
  if (!check(action.getLastStatus() == CoinRenderAction::UNSUPPORTED && before == after &&
                 serial == target->getLastSubmissionSerial(),
             "invalid combine preserves publication"))
    return false;
  scene.combine->rgbScale = 1;
  action.apply(scene.root);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                 serial < target->getLastSubmissionSerial(),
             "valid request recovers after malformed combine"))
    return false;
  auto rejectsWithoutPublication = [&](const char* message) {
    target->readbackRGBA(before);
    const uint64_t previous = target->getLastSubmissionSerial();
    action.apply(scene.root);
    target->readbackRGBA(after);
    return check((action.getLastStatus() == CoinRenderAction::UNSUPPORTED ||
                  action.getLastStatus() == CoinRenderAction::INVALID_SCENE) &&
                     before == after && previous == target->getLastSubmissionSerial(),
                 message);
  };
  // A malformed program in a separator must not leak into the following shape.
  auto* scoped = new SoSeparator;
  auto* scopedUnit = new SoTextureUnit;
  scopedUnit->unit = 1;
  scoped->addChild(scopedUnit);
  auto* scopedCombine = new SoTextureCombine;
  scopedCombine->rgbScale = 3;
  scoped->addChild(scopedCombine);
  scene.root->insertChild(scoped, scene.root->getNumChildren() - 1);
  action.apply(scene.root);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS,
             "separator restores texture combine state"))
    return false;
  scene.root->removeChild(scoped);
  const SbVec2f restored[] = {SbVec2f(0, 0), SbVec2f(1, 0), SbVec2f(1, 1), SbVec2f(0, 1)};
  scene.uv[1]->point.setNum(1);
  if (!rejectsWithoutPublication("invalid additional UV index preserves publication"))
    return false;
  scene.uv[1]->point.setValues(0, 4, restored);
  action.apply(scene.root);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS, "UV repair recovers"))
    return false;
  auto* overflowUnit = new SoTextureUnit;
  overflowUnit->unit = 8;
  auto* overflowCombine = new SoTextureCombine;
  scene.root->insertChild(overflowUnit, scene.root->getNumChildren() - 1);
  scene.root->insertChild(overflowCombine, scene.root->getNumChildren() - 1);
  if (!rejectsWithoutPublication("ninth texture unit is rejected before publication"))
    return false;
  scene.root->removeChild(overflowCombine);
  scene.root->removeChild(overflowUnit);
  action.apply(scene.root);
  if (!check(action.getLastStatus() == CoinRenderAction::SUCCESS, "unit limit rejection recovers"))
    return false;
  if (referenceRequired)
    std::cout << "P08 Coin GL reference passed (CPU=" << cpu << ")\n";
  return true;
}
} // namespace
int main() {
  SoDB::init();
  CoinRenderAction::initClass();
  if (!lineDetailContract() || !rasterContract(true) || !contract(true))
    return 1;
  if (!CoinRenderAction::isGpuBackendAvailable())
    return 77;
  if (!rasterContract(false) || !contract(false))
    return 1;
  std::cout << "P08 multitexture/combine and textured strokes passed\n";
  return 0;
}
