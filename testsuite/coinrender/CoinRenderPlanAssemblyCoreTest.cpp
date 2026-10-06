#include "rendering/coinrender/CoinRenderPlanAssemblyCore.h"
#include "rendering/coinrender/CoinRenderImageCore.h"
#include "rendering/coinrender/CoinRenderTextureCoordinateCore.h"
#include "rendering/coinrender/CoinRenderStateCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#define CHECK(c) do { if (!(c)) { std::cerr << "Failed: " << #c << " at " << __LINE__ << '\n'; return 1; } } while (0)

namespace {
CoinRenderDrawPacket emptyDraw(const CoinRenderFramePlan & plan) {
  CoinRenderDrawPacket draw;
  draw.geometry.firstVertex = static_cast<uint32_t>(plan.vertices.size());
  draw.geometry.firstIndex = static_cast<uint32_t>(plan.indices.size());
  return draw;
}
void appendCube(CoinRenderFramePlan & plan, CoinRenderCubeGeometryCore & cache,
                const float (&dimensions)[3], uint32_t material = 0, int binding = 0) {
  const size_t firstVertex = plan.vertices.size(), firstIndex = plan.indices.size(), firstDraw = plan.draws.size();
  for (size_t i = 0; i < 24; ++i) {
    CoinRenderVertexSnapshot vertex;
    for (size_t axis = 0; axis < 3; ++axis)
      vertex.position[axis] = dimensions[axis] * (((i >> axis) & 1) ? .5f : -.5f);
    vertex.normal[i % 3] = (i & 1) ? 1.f : -1.f;
    vertex.texcoord[0] = float(i % 4) / 4; vertex.texcoord[1] = float(i / 4) / 8;
    vertex.materialSlot = material;
    plan.vertices.push_back(vertex);
  }
  for (uint32_t i = 0; i < 36; ++i) plan.indices.push_back(static_cast<uint32_t>(firstVertex) + i % 24);
  auto draw = emptyDraw(plan);
  draw.geometry.firstVertex = static_cast<uint32_t>(firstVertex);
  draw.geometry.firstIndex = static_cast<uint32_t>(firstIndex);
  draw.geometry.vertexCount = 24; draw.geometry.indexCount = 36;
  plan.draws.push_back(draw);
  cache.learn(plan, firstVertex, firstIndex, firstDraw, dimensions, binding);
}
bool sameCube(const CoinRenderFramePlan & plan, const CoinRenderDrawPacket & actual,
              const CoinRenderFramePlan & source, const CoinRenderDrawPacket & expected, uint32_t material) {
  if (actual.geometry.vertexCount != 24 || actual.geometry.indexCount < 36) return false;
  for (size_t i = 0; i < 24; ++i) {
    auto vertex = source.vertices[expected.geometry.firstVertex + i]; vertex.materialSlot = material;
    if (std::memcmp(&vertex, &plan.vertices[actual.geometry.firstVertex + i], sizeof(vertex)) != 0) return false;
  }
  for (size_t i = 0; i < 36; ++i)
    if (plan.indices[actual.geometry.firstIndex + i] - actual.geometry.firstVertex !=
        source.indices[expected.geometry.firstIndex + i] - expected.geometry.firstVertex) return false;
  return true;
}
int cubeTemplates() {
  const float a[3] = {2, 2, 2}, b[3] = {2, 3, 2};
  CoinRenderCubeGeometryCore cache;
  CoinRenderFramePlan plan;
  appendCube(plan, cache, a);
  const CoinRenderFramePlan anchor = plan;
  appendCube(plan, cache, b);
  auto draw = emptyDraw(plan);
  CHECK(cache.replay(plan, draw, 0, a, 0));
  CHECK(cache.templateCount() == 2 && plan.vertices.size() == 48 && draw.geometry.firstVertex == 0);
  CHECK(sameCube(plan, draw, anchor, anchor.draws[0], 0));
  auto materialDraw = emptyDraw(plan);
  CHECK(cache.replay(plan, materialDraw, 7, a, 0));
  CHECK(sameCube(plan, materialDraw, anchor, anchor.draws[0], 7));
  const auto vertices = plan.vertices.size(), indices = plan.indices.size();
  auto repeatedMaterial = emptyDraw(plan);
  CHECK(cache.replay(plan, repeatedMaterial, 7, a, 0));
  CHECK(plan.vertices.size() == vertices && plan.indices.size() == indices &&
        repeatedMaterial.geometry.firstVertex == materialDraw.geometry.firstVertex);
  // A different source occurrence with the same captured values keeps the
  // first range. Source ownership remains the traversal layer's responsibility.
  appendCube(plan, cache, a, 7);
  auto equalSource = emptyDraw(plan);
  CHECK(cache.replay(plan, equalSource, 7, a, 0) && equalSource.geometry.firstVertex == materialDraw.geometry.firstVertex);

  // Touch A between distinct captures so B, rather than the hot A template,
  // leaves when the 33rd key is admitted. Earlier payload is never removed.
  for (size_t i = 0; i < 31; ++i) {
    const float next[3] = {float(4 + i), 2, 2};
    appendCube(plan, cache, next);
    auto hot = emptyDraw(plan); CHECK(cache.replay(plan, hot, 0, a, 0));
  }
  CHECK(cache.templateCount() == 32 && cache.templateEvictionCount() == 1);
  CHECK(cache.matches(a, 0) && !cache.matches(b, 0));
  CHECK(std::memcmp(plan.vertices.data(), anchor.vertices.data(), anchor.vertices.size() * sizeof(CoinRenderVertexSnapshot)) == 0);
  CHECK(std::equal(anchor.indices.begin(), anchor.indices.end(), plan.indices.begin()));
  const auto beforeMiss = plan.vertices.size();
  auto miss = emptyDraw(plan);
  CHECK(!cache.replay(plan, miss, 0, b, 0) && plan.vertices.size() == beforeMiss && miss.geometry.indexCount == 0);

  // Material rows are also LRU. Reusing a hot row protects it; eviction only
  // forgets metadata, including when an older packet later needs extension.
  cache.reset(); plan = CoinRenderFramePlan(); appendCube(plan, cache, a);
  CoinRenderDrawPacket evictedDraw;
  for (uint32_t slot = 1; slot < 64; ++slot) {
    auto next = emptyDraw(plan); CHECK(cache.replay(plan, next, slot, a, 0));
    CHECK(sameCube(plan, next, anchor, anchor.draws[0], slot));
    if (slot == 1) evictedDraw = next;
  }
  CHECK(cache.rangeCount() == 64);
  auto hot = emptyDraw(plan); CHECK(cache.replay(plan, hot, 0, a, 0));
  auto overflow = emptyDraw(plan); CHECK(cache.replay(plan, overflow, 64, a, 0));
  CHECK(cache.rangeCount() == 64 && cache.rangeEvictionCount() == 1);
  const auto oldSize = plan.vertices.size();
  auto stillHot = emptyDraw(plan); CHECK(cache.replay(plan, stillHot, 0, a, 0));
  CHECK(plan.vertices.size() == oldSize && stillHot.geometry.firstVertex == 0);
  auto forgotten = emptyDraw(plan); CHECK(cache.replay(plan, forgotten, 1, a, 0));
  CHECK(plan.vertices.size() == oldSize + 24 && forgotten.geometry.firstVertex != evictedDraw.geometry.firstVertex);
  const auto oldIndices = plan.indices;
  const auto beforeExtend = plan.vertices.size();
  CHECK(cache.replay(plan, evictedDraw, 1, a, 0));
  CHECK(plan.vertices.size() == beforeExtend && evictedDraw.geometry.vertexCount == 24 && evictedDraw.geometry.indexCount == 72);
  CHECK(evictedDraw.geometry.firstIndex == oldIndices.size());
  CHECK(std::equal(oldIndices.begin(), oldIndices.end(), plan.indices.begin()));
  CHECK(sameCube(plan, evictedDraw, anchor, anchor.draws[0], 1));
  for (size_t i = 0; i < 36; ++i)
    CHECK(plan.indices[evictedDraw.geometry.firstIndex + i] == plan.indices[evictedDraw.geometry.firstIndex + 36 + i]);

  // Fill both explicit bounds, including all 2048 range rows.
  cache.reset(); plan = CoinRenderFramePlan();
  for (size_t i = 0; i < 32; ++i) {
    const float next[3] = {float(1 + i), 3, 4};
    appendCube(plan, cache, next);
    for (uint32_t slot = 1; slot < 64; ++slot) {
      auto nextDraw = emptyDraw(plan); CHECK(cache.replay(plan, nextDraw, slot, next, 0));
    }
    CHECK(cache.templateCount() <= 32 && cache.rangeCount() <= 2048);
  }
  CHECK(cache.templateCount() == 32 && cache.rangeCount() == 2048 && cache.rangeEvictionCount() == 0);

  // Keys compare dimensions by bytes and retain the binding separately.
  cache.reset(); plan = CoinRenderFramePlan();
  const float positiveZero[3] = {0, 2, 2}, negativeZero[3] = {-0.0f, 2, 2};
  appendCube(plan, cache, positiveZero);
  CHECK(!cache.matches(negativeZero, 0) && !cache.matches(positiveZero, 1));
  appendCube(plan, cache, negativeZero);
  appendCube(plan, cache, positiveZero, 0, 1);
  CHECK(cache.templateCount() == 3 && cache.matches(negativeZero, 0) && cache.matches(positiveZero, 1));

  // Invalid learning never erases a valid template or mutates captured data.
  cache.reset(); plan = CoinRenderFramePlan(); appendCube(plan, cache, a);
  auto invalid = anchor; invalid.vertices[5].materialSlot = 1;
  cache.learn(invalid, 0, 0, 0, b, 0);
  CHECK(cache.matches(a, 0) && !cache.matches(b, 0));
  invalid = anchor; invalid.indices[0] = 24;
  cache.learn(invalid, 0, 0, 0, b, 0);
  cache.learn(invalid, std::numeric_limits<size_t>::max(), 0, 0, b, 0);
  cache.learn(invalid, 0, std::numeric_limits<size_t>::max(), 0, b, 0);
  cache.learn(invalid, 0, 0, std::numeric_limits<size_t>::max(), b, 0);
  CHECK(cache.matches(a, 0) && !cache.matches(b, 0));
  invalid = anchor; invalid.vertices.push_back(invalid.vertices.back());
  cache.learn(invalid, 0, 0, 0, b, 0);
  CHECK(cache.matches(a, 0) && !cache.matches(b, 0));
  // An unexpected full native snapshot for an existing key disables optional
  // reuse for this frame instead of accepting different attributes/topology.
  invalid = anchor; invalid.vertices[0].texcoord[0] = .875f;
  cache.learn(invalid, 0, 0, 0, a, 0);
  CHECK(!cache.matches(a, 0) && cache.templateCount() == 0);
  cache.learn(anchor, 0, 0, 0, a, 0);
  CHECK(!cache.matches(a, 0));
  cache.reset(); cache.learn(anchor, 0, 0, 0, a, 0);
  CHECK(cache.matches(a, 0) && cache.templateEvictionCount() == 0 && cache.rangeEvictionCount() == 0 && cache.rangeReuseCount() == 0);
  invalid = anchor; std::swap(invalid.indices[0], invalid.indices[1]);
  cache.learn(invalid, 0, 0, 0, a, 0);
  CHECK(!cache.matches(a, 0));
  return 0;
}
}
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
  {
    CoinRenderFramePlan repeated;
    CoinRenderPlanAssemblyCore::StateIndex index;
    // Many transforms, each with multiple appearances, exercise both common
    // unique-model capture and repeated lookups of every prior variant.
    for (uint32_t model = 0; model < 2048; ++model) {
      CoinRenderRenderStateSnapshot captured;
      captured.model.setTranslate(SbVec3f(float(model),float(model % 13),0));
      for (uint32_t appearance = 0; appearance < 4; ++appearance) {
        captured.materialSlot = appearance;
        captured.depthWrite = appearance != 3;
        CHECK(CoinRenderPlanAssemblyCore::state(repeated,index,captured) == model*4+appearance);
      }
    }
    for (uint32_t model = 2048; model-- > 0;) {
      CoinRenderRenderStateSnapshot captured;
      captured.model.setTranslate(SbVec3f(float(model),float(model % 13),-0.0f));
      for (uint32_t appearance = 4; appearance-- > 0;) {
        captured.materialSlot = appearance;
        captured.depthWrite = appearance != 3;
        CHECK(CoinRenderPlanAssemblyCore::state(repeated,index,captured) == model*4+appearance);
      }
    }
    CHECK(repeated.renderStates.size() == 8192);
    // A new frame cannot inherit slots from the previous plan.
    repeated.renderStates.clear(); index.clear();
    CHECK(CoinRenderPlanAssemblyCore::state(repeated,index,CoinRenderRenderStateSnapshot{}) == 0);
  }
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
  CoinRenderVertexSnapshot coordinate;
  coordinate.texcoord[0] = 2; coordinate.texcoord[1] = 4;
  coordinate.textureR[0] = 3; coordinate.textureQ[0] = 2;
  CHECK(coin_render_texture_coordinate(coordinate, 0) == SbVec4f(2,4,3,2));
  SbMatrix textureMatrix = SbMatrix::identity();
  textureMatrix[2][0] = 2; textureMatrix[2][3] = 1;
  CHECK(coin_render_transformed_texture_coordinate(coordinate, 0, textureMatrix) == SbVec4f(8,4,3,5));
  CoinRenderRenderStateSnapshot projectedState, directState;
  directState.textureProjection = CoinRenderTextureProjection::DIRECT_ST;
  CHECK(!coin_render_same_state_except_camera(projectedState, directState));
  CoinRenderFramePlan projectionPlan;
  CoinRenderPlanAssemblyCore::StateIndex projectionIndex;
  CHECK(CoinRenderPlanAssemblyCore::state(projectionPlan, projectionIndex, projectedState) == 0);
  CHECK(CoinRenderPlanAssemblyCore::state(projectionPlan, projectionIndex, directState) == 1);
  CHECK(coin_render_project_texture_coordinate(SbVec3f(4,8,2), CoinRenderTextureProjection::PROJECTIVE) == SbVec2f(2,4));
  CHECK(coin_render_project_texture_coordinate(SbVec3f(4,8,0), CoinRenderTextureProjection::DIRECT_ST) == SbVec2f(4,8));

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
  cube.reset(false); // The optout preserves the previous one-template, 32-range path.
  const float dimensions[3] = {2,2,2}, changed[3] = {2,3,2};
  CHECK(!cube.matches(dimensions, 0));
  cube.learn(mesh, 0, 0, 0, dimensions, 0);
  CHECK(cube.matches(dimensions, 0) && !cube.matches(changed, 0) && !cube.matches(dimensions, 1));
  CoinRenderDrawPacket draw;
  draw.geometry.firstVertex = 24; draw.geometry.firstIndex = 36;
  CHECK(cube.replay(mesh, draw, 0, dimensions, 0));
  CHECK(mesh.vertices.size() == 24 && mesh.indices.size() == 36);
  CHECK(draw.geometry.firstVertex == 0 && draw.geometry.indexCount == 36);
  mesh.indices.push_back(23); // Another draw owns this index; earlier ranges stay intact.
  const auto original = mesh.indices;
  CoinRenderPlanAssemblyCore::makeIndicesAppendable(mesh, draw.geometry);
  CHECK(draw.geometry.firstIndex == 37 && mesh.indices.size() == 73);
  CHECK(std::equal(original.begin(), original.end(), mesh.indices.begin()));
  CHECK(cube.replay(mesh, draw, 0, dimensions, 0));
  CHECK(mesh.vertices.size() == 24 && draw.geometry.indexCount == 72);
  for (uint32_t materialSlot = 1; materialSlot <= 34; ++materialSlot) {
    CoinRenderDrawPacket next;
    next.geometry.firstVertex = static_cast<uint32_t>(mesh.vertices.size());
    next.geometry.firstIndex = static_cast<uint32_t>(mesh.indices.size());
    CHECK(cube.replay(mesh, next, materialSlot, dimensions, 0));
    CHECK(next.geometry.vertexCount == 24 && next.geometry.indexCount == 36);
    CHECK(mesh.vertices[next.geometry.firstVertex].materialSlot == materialSlot);
  }
  auto count = mesh.vertices.size();
  CoinRenderDrawPacket uncached;
  uncached.geometry.firstVertex = static_cast<uint32_t>(count);
  uncached.geometry.firstIndex = static_cast<uint32_t>(mesh.indices.size());
  CHECK(cube.replay(mesh, uncached, 34, dimensions, 0));
  CHECK(mesh.vertices.size() == count + 24); // Bounded cache does not keep every material.
  CHECK(cube.replay(mesh, uncached, 34, dimensions, 0));
  CHECK(mesh.vertices.size() == count + 48 && uncached.geometry.vertexCount == 48); // Legacy merged layout remains exact.
  cube.reset();
  CHECK(!cube.matches(dimensions, 0));
  CHECK(cubeTemplates() == 0);
  mesh.vertices.resize(24); mesh.indices.resize(36); mesh.draws.resize(1);
  mesh.indices[0] = 99;
  cube.learn(mesh, 0, 0, 0, dimensions, 0);
  CHECK(!cube.matches(dimensions, 0));
  std::cout << "Captured values, image conversion, deduplication and bounded cube ranges passed\n";
  return 0;
}
