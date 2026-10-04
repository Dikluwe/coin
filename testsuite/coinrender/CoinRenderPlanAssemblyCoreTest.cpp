#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include <cmath>
#include <iostream>
#include <limits>
#define CHECK(c) do { if (!(c)) { std::cerr << "Failed: " << #c << " at " << __LINE__ << '\n'; return 1; } } while (0)
int main() {
  // No traversal, SoDB initialization or GPU is needed to assemble captured values.
  CoinRenderFramePlan plan;
  CoinRenderPlanAssemblyCore::StateIndex lookup;
  CoinRenderRenderStateSnapshot state;
  const auto slot = CoinRenderPlanAssemblyCore::state(plan, lookup, state);
  state.model[0][1] = -0.0f;
  CHECK(CoinRenderPlanAssemblyCore::state(plan, lookup, state) == slot);
  state.shadowStyle = 1;
  CHECK(CoinRenderPlanAssemblyCore::state(plan, lookup, state) != slot);
  state.shadowStyle = 3;
  state.extraTextures[2].enabled = true;
  CHECK(CoinRenderPlanAssemblyCore::state(plan, lookup, state) != slot);
  CoinRenderMaterialSnapshot material;
  CHECK(CoinRenderPlanAssemblyCore::material(plan, material) == 0);
  CHECK(CoinRenderPlanAssemblyCore::material(plan, material) == 0);
  material.transparency = 0.4f;
  CHECK(CoinRenderPlanAssemblyCore::material(plan, material) == 1);
  CoinRenderLightingSnapshot lighting;
  lighting.lights.emplace_back();
  CHECK(CoinRenderPlanAssemblyCore::lighting(plan, lighting) == 0);
  lighting.lights[0].sourceRevision = 42;
  CHECK(CoinRenderPlanAssemblyCore::lighting(plan, lighting) == 1);
  CoinRenderCameraSnapshot camera;
  camera.nearDistance = 0; camera.farDistance = -1;
  CoinRenderPlanAssemblyCore::normalizeCamera(camera);
  CHECK(camera.nearDistance == 0.1f && camera.farDistance == 100.1f);
  CHECK(CoinRenderPlanAssemblyCore::camera(plan, camera) == 0);
  camera.nearDistance += 1e-6f;
  CHECK(CoinRenderPlanAssemblyCore::camera(plan, camera) == 0);
  camera.nearDistance += 1;
  CHECK(CoinRenderPlanAssemblyCore::camera(plan, camera) == 1);
  state.fogEnd = 0; state.lineWidth = -1; state.pointSize = 0;
  state.linePatternScaleFactor = -1; state.screenDoorTransparency = 2;
  CoinRenderPlanAssemblyCore::normalizeState(state, camera);
  CHECK(state.fogEnd == camera.farDistance && state.lineWidth == 1 && state.pointSize == 1);
  CHECK(state.linePatternScaleFactor == 1 && state.screenDoorTransparency == 1);
  CoinRenderLightSourceSnapshot light;
  light.type = CoinRenderLightType::SPOT;
  light.sourceModel.setTranslate(SbVec3f(2,-1,3));
  light.position[0] = 1; light.direction[2] = -2;
  light.cutOffAngle = 2; light.dropOffRate = -1;
  CoinRenderPlanAssemblyCore::transformLight(light);
  CHECK(SbVec3f(light.position) == SbVec3f(3,-1,3));
  CHECK(SbVec3f(light.direction) == SbVec3f(0,0,-1));
  CHECK(light.cutOffAngle == 1.570796327f && light.dropOffRate == 0);
  float uv[2] = {7,8};
  CHECK(!CoinRenderPlanAssemblyCore::projectTexcoord(SbVec4f(1,2,0,0), uv));
  CHECK(uv[0] == 7 && uv[1] == 8);
  CHECK(CoinRenderPlanAssemblyCore::projectTexcoord(SbVec4f(2,4,0,2), uv));
  CHECK(uv[0] == 1 && uv[1] == 2);

  const uint8_t source[] = {10,20,30,40,50,60,70,80};
  const std::vector<uint8_t> expected[] = {
    {10,10,10,255,20,20,20,255}, {10,10,10,20,30,30,30,40},
    {10,20,30,255,40,50,60,255}, {10,20,30,40,50,60,70,80}};
  std::vector<uint8_t> rgba;
  for (int components = 1; components <= 4; ++components) {
    CHECK(CoinRenderImageCore::convertToRgba8(source, 2, components, rgba));
    CHECK(rgba == expected[components - 1]);
  }
  const auto previous = rgba;
  CHECK(!CoinRenderImageCore::convertToRgba8(source, 2, 5, rgba) && rgba == previous);
  CHECK(!CoinRenderImageCore::convertToRgba8(nullptr, 2, 4, rgba) && rgba == previous);
  CHECK(!CoinRenderImageCore::convertToRgba8(source, std::numeric_limits<size_t>::max(), 4, rgba) && rgba == previous);
  CoinRenderTextureImageSnapshot texture;
  texture.width = 2; texture.height = 1; texture.pixelsRgba = rgba;
  texture.contentDigest = CoinRenderImageCore::rgba8Digest(rgba);
  auto copy = texture;
  CHECK(CoinRenderPlanAssemblyCore::texture(plan, std::move(copy)) == 0);
  copy = texture;
  CHECK(CoinRenderPlanAssemblyCore::texture(plan, std::move(copy)) == 0);
  copy = texture; copy.pixelsRgba[0] = 0; // Equal digest cannot hide differing bytes.
  CHECK(CoinRenderPlanAssemblyCore::texture(plan, std::move(copy)) == 1);

  CoinRenderFramePlan mesh;
  mesh.vertices.resize(24); mesh.draws.emplace_back();
  for (uint32_t i = 0; i < 36; ++i) mesh.indices.push_back(i % 24);
  CoinRenderCubeGeometryCore cube;
  const float dimensions[3] = {2,2,2}, changed[3] = {2,3,2};
  CHECK(!cube.matches(dimensions, 0));
  cube.learn(mesh, 0, 0, 0, dimensions, 0);
  CHECK(cube.matches(dimensions, 0) && !cube.matches(changed, 0) && !cube.matches(dimensions, 1));
  CoinRenderDrawPacket draw;
  draw.geometry.firstVertex = 24; draw.geometry.firstIndex = 36;
  cube.replay(mesh, draw, 0);
  CHECK(mesh.vertices.size() == 24 && mesh.indices.size() == 36);
  CHECK(draw.geometry.firstVertex == 0 && draw.geometry.indexCount == 36);
  mesh.indices.push_back(23); // Another draw owns this index; earlier ranges stay intact.
  const auto original = mesh.indices;
  CoinRenderPlanAssemblyCore::makeIndicesAppendable(mesh, draw.geometry);
  CHECK(draw.geometry.firstIndex == 37 && mesh.indices.size() == 73);
  CHECK(std::equal(original.begin(), original.end(), mesh.indices.begin()));
  cube.replay(mesh, draw, 0);
  CHECK(mesh.vertices.size() == 24 && draw.geometry.indexCount == 72);
  for (uint32_t materialSlot = 1; materialSlot <= 34; ++materialSlot) {
    CoinRenderDrawPacket next;
    next.geometry.firstVertex = static_cast<uint32_t>(mesh.vertices.size());
    next.geometry.firstIndex = static_cast<uint32_t>(mesh.indices.size());
    cube.replay(mesh, next, materialSlot);
    CHECK(next.geometry.vertexCount == 24 && next.geometry.indexCount == 36);
    CHECK(mesh.vertices[next.geometry.firstVertex].materialSlot == materialSlot);
  }
  auto count = mesh.vertices.size();
  CoinRenderDrawPacket uncached;
  uncached.geometry.firstVertex = static_cast<uint32_t>(count);
  uncached.geometry.firstIndex = static_cast<uint32_t>(mesh.indices.size());
  cube.replay(mesh, uncached, 34);
  CHECK(mesh.vertices.size() == count + 24); // Bounded cache does not keep every material.
  cube.reset();
  CHECK(!cube.matches(dimensions, 0));
  mesh.vertices.resize(24); mesh.indices.resize(36); mesh.draws.resize(1);
  mesh.indices[0] = 99;
  cube.learn(mesh, 0, 0, 0, dimensions, 0);
  CHECK(!cube.matches(dimensions, 0));
  std::cout << "Captured values, image conversion, deduplication and bounded cube ranges passed\n";
  return 0;
}
