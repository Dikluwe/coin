// This is a capture adapter, not a rasterizer. No CoinRenderTarget is created.
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoRotationXYZ.h>
#include <Inventor/nodes/SoCube.h>
#include "actions/CoinRenderActionP.h"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
SoSeparator * scene = nullptr;
SoRotationXYZ * rotation = nullptr;
std::unique_ptr<CoinRenderAction> action;
std::vector<float> vertices;
std::vector<uint32_t> indices;
std::string error;
void initialize() {
  SoDB::init();
  CoinRenderAction::initClass();
  scene = new SoSeparator; scene->ref();
  auto * camera = new SoPerspectiveCamera;
  camera->position.setValue(0, 0, 6);
  camera->nearDistance = 0.1f; camera->farDistance = 100.0f;
  scene->addChild(camera);
  auto * material = new SoMaterial;
  material->diffuseColor.setValue(0.15f, 0.65f, 0.95f);
  scene->addChild(material);
  rotation = new SoRotationXYZ; rotation->axis = SoRotationXYZ::Y;
  scene->addChild(rotation);
  auto * tilt = new SoRotationXYZ; tilt->axis = SoRotationXYZ::X; tilt->angle = 0.35f;
  scene->addChild(tilt);
  scene->addChild(new SoCube);
  action.reset(new CoinRenderAction);
}
}
extern "C" {
int coin_capture(float angle, int width, int height) {
  error.clear(); vertices.clear(); indices.clear();
  if (!std::isfinite(angle) || width < 1 || height < 1 || width > 8192 || height > 8192) {
    error = "Invalid capture angle or viewport (1..8192)"; return 0;
  }
  if (!scene) initialize();
  rotation->angle = angle;
  action->setViewportRegion(SbViewportRegion(static_cast<short>(width), static_cast<short>(height)));
  action->apply(scene); // CoinRender's targetless path only captures and records.
  if (action->getLastStatus() != CoinRenderAction::SUCCESS) {
    error = action->getLastError().getString(); return 0;
  }
  const auto & plan = action->getPimpl()->lastValidPlan;
  if (plan.cameras.empty() || plan.materials.empty() || plan.draws.empty()) {
    error = "CoinRender did not capture camera, material and geometry"; return 0;
  }
  for (const auto & draw : plan.draws) {
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || draw.renderStateSlot >= plan.renderStates.size()) {
      error = "Unsupported captured draw in WebGPU demonstration profile"; return 0;
    }
    const auto & state = plan.renderStates[draw.renderStateSlot];
    const SbMatrix modelView = state.model * state.view;
    const SbMatrix mvp = modelView * state.projectionCoin;
    const uint32_t base = static_cast<uint32_t>(vertices.size() / 11);
    for (uint32_t i = 0; i < draw.geometry.vertexCount; ++i) {
      const auto & vertex = plan.vertices.at(draw.geometry.firstVertex + i);
      SbVec4f clip;
      mvp.multVecMatrix(SbVec4f(vertex.position[0], vertex.position[1], vertex.position[2], 1), clip);
      SbVec3f normal;
      modelView.multDirMatrix(SbVec3f(vertex.normal), normal); normal.normalize();
      const auto & material = plan.materials.at(vertex.materialSlot);
      vertices.insert(vertices.end(), {clip[0], clip[1], (clip[2] + clip[3])*0.5f, clip[3],
        normal[0], normal[1], normal[2], material.diffuse[0], material.diffuse[1], material.diffuse[2], material.diffuse[3]});
    }
    for (uint32_t i = 0; i < draw.geometry.indexCount; ++i) {
      const uint32_t index = plan.indices.at(draw.geometry.firstIndex + i);
      if (index < draw.geometry.firstVertex || index >= draw.geometry.firstVertex + draw.geometry.vertexCount) {
        error = "Captured index outside draw range"; return 0;
      }
      indices.push_back(base + index - draw.geometry.firstVertex);
    }
  }
  if (vertices.empty() || indices.empty()) { error = "Empty captured mesh"; return 0; }
  return 1;
}
const float * coin_vertices() { return vertices.data(); }
unsigned coin_vertex_count() { return static_cast<unsigned>(vertices.size()/11); }
const uint32_t * coin_indices() { return indices.data(); }
unsigned coin_index_count() { return static_cast<unsigned>(indices.size()); }
const char * coin_error() { return error.c_str(); }
}
