#ifndef COIN_RENDER_PORTABLE_SHADER_MODULE_SCENE_H
#define COIN_RENDER_PORTABLE_SHADER_MODULE_SCENE_H
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShaderParameter.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <Inventor/rendering/CoinRenderTarget.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace portable_module_test {
using Color = std::array<float, 4>;
inline bool check(bool value, const std::string &label) {
  if (!value)
    std::cerr << "portable_module FAIL " << label << std::endl;
  return value;
}
struct Scene {
  SoSeparator *root = new SoSeparator;
  SoShaderProgram *program = new SoShaderProgram;
  SoFragmentShader *fragment = new SoFragmentShader;
  SoCoordinate3 *positions = new SoCoordinate3;
  SoIndexedFaceSet *mesh = new SoIndexedFaceSet;
  SoMaterial *material = nullptr;
  SoLightModel *model = nullptr;
  SoOrthographicCamera *camera = nullptr;
  std::vector<SoUniformShaderParameter *> owned;
  bool attached = false;
  Scene() {
    root->ref();
    program->ref();
    fragment->sourceType = SoShaderObject::GLSL_PROGRAM;
    fragment->sourceProgram =
        "void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}";
    fragment->isActive = TRUE;
    program->shaderObject.set1Value(0, fragment);
    camera = new SoOrthographicCamera;
    camera->position.setValue(0, 0, 3);
    camera->height = 2;
    camera->nearDistance = .1f;
    camera->farDistance = 10;
    root->addChild(camera);
    model = new SoLightModel;
    model->model = SoLightModel::BASE_COLOR;
    root->addChild(model);
    material = new SoMaterial;
    material->diffuseColor.setValue(.6f, .3f, .8f);
    material->transparency = 0;
    root->addChild(material);
    auto *depth = new SoDepthBuffer;
    depth->test = TRUE;
    depth->write = TRUE;
    depth->function = SoDepthBuffer::LESS;
    root->addChild(depth);
    const SbVec3f p[] = {SbVec3f(-.75f, -.71875f, 0),
                         SbVec3f(.75f, -.71875f, 0), SbVec3f(.75f, .75f, 0),
                         SbVec3f(-.75f, .75f, 0)};
    positions->point.setValues(0, 4, p);
    root->addChild(positions);
    const int32_t indices[] = {0, 1, 2, -1, 0, 2, 3, -1};
    mesh->coordIndex.setValues(0, 8, indices);
    root->addChild(mesh);
  }
  ~Scene() {
    root->unref();
    program->unref();
    for (auto *p : owned)
      p->unref();
  }
  void attach(bool on) {
    if (on && !attached)
      root->insertChild(program, root->findChild(positions));
    if (!on && attached)
      root->removeChild(program);
    attached = on;
  }
  void clearParameters() {
    fragment->parameter.setNum(0);
    for (auto *p : owned)
      p->unref();
    owned.clear();
  }
  SoShaderParameter4f *parameter4(const std::string &name, Color value) {
    auto *p = new SoShaderParameter4f;
    p->ref();
    owned.push_back(p);
    p->name = name.c_str();
    p->value.setValue(value[0], value[1], value[2], value[3]);
    fragment->parameter.set1Value(fragment->parameter.getNum(), p);
    return p;
  }
};
struct Frame {
  std::vector<uint8_t> rgba;
  std::vector<float> depth;
  uint64_t serial = 0;
};
bool image(const Frame &f, Color expected) {
  constexpr unsigned side = 64;
  if (f.rgba.size() != side * side * 4 || f.depth.size() != side * side)
    return check(false, "complete real color and depth readback");
  unsigned inside = 0, outside = 0, bad = 0, depthBad = 0;
  const double margin = 2.1 * 2 / side;
  for (unsigned y = 0; y < side; ++y)
    for (unsigned x = 0; x < side; ++x) {
      const double wx = (x + .5) * 2 / side - 1, wy = 1 - (y + .5) * 2 / side;
      const bool in = wx > -.75 + margin && wx < .75 - margin &&
                      wy > -.71875 + margin && wy < .75 - margin;
      const bool out = wx < -.75 - margin || wx > .75 + margin ||
                       wy < -.71875 - margin || wy > .75 + margin;
      const size_t p = size_t(y) * side + x;
      if (in) {
        ++inside;
        for (unsigned c = 0; c < 4; ++c)
          bad += std::abs(f.rgba[p * 4 + c] - 255. * expected[c]) >
                 (c == 3 ? 1 : 3);
        depthBad += std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
      }
      if (out) {
        ++outside;
        for (unsigned c = 0; c < 4; ++c)
          bad += f.rgba[p * 4 + c] > (c == 3 ? 1 : 3);
        depthBad += f.depth[p] != 1;
      }
      depthBad += f.depth[p] != 1 && std::abs(f.depth[p] - 2.9 / 9.9) > 2e-5;
    }
  std::cout << "portable_module_image inside=" << inside
            << " outside=" << outside << " bad=" << bad
            << " depth_bad=" << depthBad << std::endl;
  return check(inside > 1000 && outside > 500 && !bad && !depthBad,
               "independent authored RGBA/interior/background/geometric depth");
}
} // namespace portable_module_test
#endif
