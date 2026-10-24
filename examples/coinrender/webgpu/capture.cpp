// This is a capture adapter, not a rasterizer. No CoinRenderTarget is created.
#include <Inventor/SoDB.h>
#include <Inventor/actions/CoinRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoRotationXYZ.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
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

// The large-scene ingress retains at most 1024 Coin instances and one frame
// plan. No million-node graph or expanded million-mesh plan is constructed.
namespace {
constexpr unsigned CITY_CHUNK = 1024;
float cityInput[CITY_CHUNK * 10] = {};
float cityBounds[6] = {};
float cityUniform[40] = {};
std::vector<float> cityMesh;
std::vector<uint32_t> cityIndices;
std::vector<float> cityInstances;
SoPerspectiveCamera * cityCamera = nullptr;
SoCube * cityCube = nullptr;

void addCityState(SoSeparator * root) {
  root->addChild(cityCamera);
  auto * key = new SoDirectionalLight;
  key->direction.setValue(-0.5f, -1, -0.3f); key->intensity = 0.85f;
  root->addChild(key);
  auto * fill = new SoDirectionalLight;
  fill->direction.setValue(0.7f, -0.4f, 0.5f); fill->intensity = 0.35f;
  root->addChild(fill);
}
CoinRenderFramePlan captureCityPlan(SoSeparator * root, int width, int height) {
  CoinRenderAction capture(SbViewportRegion(static_cast<short>(width), static_cast<short>(height)));
  capture.apply(root);
  if (capture.getLastStatus() != CoinRenderAction::SUCCESS)
    throw std::runtime_error(capture.getLastError().getString());
  return std::move(capture.getPimpl()->lastValidPlan);
}
void fitCityCamera(int width, int height) {
  if (width < 1 || height < 1 || width > 8192 || height > 8192)
    throw std::runtime_error("Invalid city viewport");
  auto * boundsRoot = new SoSeparator; boundsRoot->ref();
  auto * pose = new SoTransform;
  auto * box = new SoCube;
  SbVec3f center;
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(cityBounds[axis]) || !std::isfinite(cityBounds[axis+3]) ||
        cityBounds[axis+3] <= cityBounds[axis]) {
      boundsRoot->unref(); throw std::runtime_error("Invalid city bounds");
    }
    center[axis] = (cityBounds[axis] + cityBounds[axis+3]) * 0.5f;
  }
  pose->translation = center;
  box->width = cityBounds[3] - cityBounds[0];
  box->height = cityBounds[4] - cityBounds[1];
  box->depth = cityBounds[5] - cityBounds[2];
  boundsRoot->addChild(pose); boundsRoot->addChild(box);
  cityCamera->orientation.setValue(SbRotation(SbVec3f(0,0,-1), SbVec3f(-0.5f,-0.35f,-1)));
  cityCamera->heightAngle = 0.785398163f;
  cityCamera->viewAll(boundsRoot, SbViewportRegion(static_cast<short>(width), static_cast<short>(height)), 1.0f);
  boundsRoot->unref();
}
void captureCityCamera(int width, int height, bool canonical) {
  fitCityCamera(width, height);
  auto * root = new SoSeparator; root->ref(); addCityState(root);
  root->addChild(cityCube);
  CoinRenderFramePlan plan;
  try { plan = captureCityPlan(root, width, height); }
  catch (...) { root->unref(); throw; }
  root->unref();
  if (plan.draws.size() != 1) throw std::runtime_error("Canonical Coin cube must produce one draw");
  const auto & draw = plan.draws.front();
  const auto & state = plan.renderStates.at(draw.renderStateSlot);
  const SbMatrix vp = state.view * state.projectionCoin;
  std::memcpy(cityUniform, vp.getValue(), 16 * sizeof(float));
  std::memcpy(cityUniform + 16, state.view.getValue(), 16 * sizeof(float));
  const auto & lights = plan.lightingStates.at(state.lightingSlot).lights;
  if (lights.size() != 2) throw std::runtime_error("City capture did not retain two authored lights");
  for (unsigned l = 0; l < 2; ++l) {
    for (unsigned a = 0; a < 3; ++a) cityUniform[32 + l*4 + a] = lights[l].direction[a];
    cityUniform[35 + l*4] = lights[l].intensity;
  }
  if (canonical) {
    if (draw.geometry.vertexCount != 24 || draw.geometry.indexCount != 36)
      throw std::runtime_error("Unexpected canonical Coin cube geometry");
    cityMesh.clear(); cityIndices.clear();
    for (unsigned i = 0; i < draw.geometry.vertexCount; ++i) {
      const auto & v = plan.vertices.at(draw.geometry.firstVertex + i);
      cityMesh.insert(cityMesh.end(), {v.position[0],v.position[1],v.position[2],v.normal[0],v.normal[1],v.normal[2]});
    }
    for (unsigned i = 0; i < draw.geometry.indexCount; ++i)
      cityIndices.push_back(plan.indices.at(draw.geometry.firstIndex + i) - draw.geometry.firstVertex);
  }
}
}
extern "C" {
float * coin_city_input() { return cityInput; }
float * coin_city_bounds() { return cityBounds; }
const float * coin_city_uniform() { return cityUniform; }
const float * coin_city_vertices() { return cityMesh.data(); }
const uint32_t * coin_city_indices() { return cityIndices.data(); }
unsigned coin_city_vertex_count() { return static_cast<unsigned>(cityMesh.size()/6); }
unsigned coin_city_index_count() { return static_cast<unsigned>(cityIndices.size()); }
const float * coin_city_instances() { return cityInstances.data(); }
unsigned coin_city_capacity() { return CITY_CHUNK; }
int coin_city_camera(int width, int height) {
  error.clear();
  try {
    if (!cityCamera) throw std::runtime_error("City capture is not initialized");
    captureCityCamera(width, height, false); return 1;
  } catch (const std::exception & e) { error = e.what(); return 0; }
}
int coin_city_begin(int width, int height) {
  error.clear();
  try {
    if (!scene) initialize();
    if (!cityCamera) { cityCamera = new SoPerspectiveCamera; cityCamera->ref(); }
    if (!cityCube) {
      cityCube = new SoCube; cityCube->ref();
      cityCube->width = 1; cityCube->height = 1; cityCube->depth = 1;
    }
    captureCityCamera(width, height, true); return 1;
  } catch (const std::exception & e) { error = e.what(); return 0; }
}
int coin_city_capture(unsigned count, int width, int height) {
  error.clear(); cityInstances.clear();
  auto * root = new SoSeparator; root->ref();
  try {
    if (!cityCamera || cityMesh.empty() || count == 0 || count > CITY_CHUNK)
      throw std::runtime_error("Invalid city capture chunk");
    addCityState(root);
    for (unsigned i = 0; i < count; ++i) {
      const float * row = cityInput + i*10;
      for (unsigned k = 0; k < 10; ++k)
        if (!std::isfinite(row[k])) throw std::runtime_error("Nonfinite city record");
      if (row[3] <= 0 || row[4] <= 0 || row[5] <= 0 || row[9] != 1)
        throw std::runtime_error("City requires positive scale and opaque material");
      auto * item = new SoSeparator;
      auto * material = new SoMaterial; material->diffuseColor.setValue(row[6],row[7],row[8]);
      material->shininess = 0.2f; item->addChild(material);
      auto * transform = new SoTransform;
      transform->translation.setValue(row[0],row[1],row[2]);
      transform->scaleFactor.setValue(row[3],row[4],row[5]); item->addChild(transform);
      item->addChild(cityCube); root->addChild(item);
    }
    auto plan = captureCityPlan(root, width, height);
    if (plan.draws.size() != count) throw std::runtime_error("Coin capture instance count mismatch");
    for (const auto & draw : plan.draws) {
      if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
          draw.geometry.vertexCount != cityMesh.size()/6 || draw.geometry.indexCount != cityIndices.size())
        throw std::runtime_error("Captured instance is not canonical cube geometry");
      for (unsigned i = 0; i < draw.geometry.vertexCount; ++i) {
        const auto & v = plan.vertices.at(draw.geometry.firstVertex+i);
        for (unsigned k = 0; k < 3; ++k)
          if (std::abs(v.position[k]-cityMesh[i*6+k]) > 0.000001f ||
              std::abs(v.normal[k]-cityMesh[i*6+3+k]) > 0.000001f)
            throw std::runtime_error("Captured canonical mesh mismatch");
      }
      for (unsigned i = 0; i < draw.geometry.indexCount; ++i)
        if (plan.indices.at(draw.geometry.firstIndex+i) - draw.geometry.firstVertex != cityIndices[i])
          throw std::runtime_error("Captured canonical index mismatch");
      const auto & state = plan.renderStates.at(draw.renderStateSlot);
      const auto & material = plan.materials.at(state.materialSlot);
      const float * matrix = &state.model.getValue()[0][0];
      cityInstances.insert(cityInstances.end(), matrix, matrix+16);
      cityInstances.insert(cityInstances.end(), material.diffuse, material.diffuse+4);
    }
    root->unref(); return static_cast<int>(cityInstances.size()/20);
  } catch (const std::exception & e) { root->unref(); error=e.what(); cityInstances.clear(); return 0; }
}
}
