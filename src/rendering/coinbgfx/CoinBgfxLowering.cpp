#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderClipCore.h"
#include "rendering/coinrender/CoinRenderTransformCore.h"
#include "rendering/coinrender/CoinRenderComposition.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <unordered_map>
#include <limits>

namespace {
struct SharedRangeKey {
  uint32_t firstVertex, vertexCount, firstIndex, indexCount;
  bool operator==(const SharedRangeKey & other) const {
    return firstVertex == other.firstVertex && vertexCount == other.vertexCount &&
      firstIndex == other.firstIndex && indexCount == other.indexCount;
  }
};

struct SharedRangeHash {
  size_t operator()(const SharedRangeKey & key) const {
    size_t hash = key.firstVertex;
    hash = hash * 31 + key.vertexCount;
    hash = hash * 31 + key.firstIndex;
    return hash * 31 + key.indexCount;
  }
};

struct SharedRange {
  uint32_t uniformMaterialSlot = 0;
  bool uniformMaterial = true;
  float maxAlpha = 0.0f;
  uint64_t materialSignature = 0;
  // First occurrence order is the existing indexed lowering order. Entries
  // belong to this invocation; Coin transforms and uniforms remain per draw.
  std::vector<uint32_t> sourceVertices;
  std::vector<uint32_t> relativeIndices;
};

uint64_t hashBytes(uint64_t hash, const void * data, size_t bytes)
{
  const unsigned char * value = static_cast<const unsigned char *>(data);
  for (size_t i = 0; i < bytes; ++i) {
    hash ^= value[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

// This optional GPU transform profile keeps wide headroom below float overflow
// in point dot products, normal length squares and projection products. Plans
// outside these broad bounds retain the existing CPU lowering path.
bool instancedMagnitude(float value)
{
  return std::isfinite(value) && std::abs(value) <= 1.0e8f;
}

bool instancedMatrixMagnitude(const SbMatrix & matrix)
{
  const auto values = matrix.getValue();
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      if (!instancedMagnitude(values[row][column])) return false;
  return true;
}

bool composableInstanceNormal(const SbMatrix & modelView, const SbMatrix & normal)
{
  const float determinant = modelView.det4();
  if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-9f) return false;
  // Verify the inverse actually used by Coin. An ill-conditioned inverse (or
  // its own identity fallback) cannot safely compose with a new camera.
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column) {
      double value = 0.0;
      for (int k = 0; k < 3; ++k) value += double(modelView[row][k]) * normal[column][k];
      if (!std::isfinite(value) || std::abs(value - (row == column ? 1.0 : 0.0)) > 1.0e-4)
        return false;
    }
  return true;
}

// Factor positions only. Captured normals remain authored attributes, rather
// than normals inferred from a scaled transport matrix. Each axis must contain
// both endpoints of a normal, positive scale, and every coordinate must round
// trip exactly through {-1, signed zero, +1}. Failed qualification leaves the
// original mesh available for byte-exact matching.
bool normalizeDiagonalMesh(std::vector<CoinBgfxInstancedVertex> & vertices,
                           float scale[3])
{
  float extent[3] = {};
  for (const auto & vertex : vertices)
    for (int axis = 0; axis < 3; ++axis)
      extent[axis] = std::max(extent[axis], std::abs(vertex.position[axis]));
  for (int axis = 0; axis < 3; ++axis)
    if (!std::isfinite(extent[axis]) || extent[axis] < std::numeric_limits<float>::min())
      return false;
  bool negative[3] = {}, positive[3] = {};
  for (const auto & vertex : vertices) {
    for (int axis = 0; axis < 3; ++axis) {
      const float value = vertex.position[axis];
      float normalized;
      if (value == extent[axis]) { normalized = 1.0f; positive[axis] = true; }
      else if (value == -extent[axis]) { normalized = -1.0f; negative[axis] = true; }
      else if (value == 0.0f) normalized = std::copysign(0.0f, value);
      else return false;
      const float restored = normalized * extent[axis];
      if (std::memcmp(&value, &restored, sizeof(float)) != 0) return false;
    }
  }
  for (int axis = 0; axis < 3; ++axis)
    if (!negative[axis] || !positive[axis]) return false;
  for (auto & vertex : vertices)
    for (int axis = 0; axis < 3; ++axis)
      vertex.position[axis] = vertex.position[axis] == 0.0f
        ? std::copysign(0.0f, vertex.position[axis])
        : std::copysign(1.0f, vertex.position[axis]);
  std::memcpy(scale, extent, sizeof(extent));
  return true;
}

void copyLighting(const CoinRenderLightingSnapshot & lighting, CoinBgfxDraw & draw)
{
  std::memcpy(draw.ambientLight, lighting.ambientColor, sizeof(lighting.ambientColor));
  draw.ambientLight[3] = lighting.ambientIntensity;
  std::memset(draw.lightCount, 0, sizeof(draw.lightCount));
  std::memset(draw.lightPositionType, 0, sizeof(draw.lightPositionType));
  std::memset(draw.lightDirectionCutoff, 0, sizeof(draw.lightDirectionCutoff));
  std::memset(draw.lightColorIntensity, 0, sizeof(draw.lightColorIntensity));
  std::memset(draw.lightAttenuationDrop, 0, sizeof(draw.lightAttenuationDrop));
  draw.lightCount[0] = static_cast<float>(lighting.lights.size());
  for (size_t i = 0; i < lighting.lights.size(); ++i) {
    const auto & light = lighting.lights[i];
    for (int channel = 0; channel < 3; ++channel) {
      draw.lightPositionType[i][channel] = light.position[channel];
      draw.lightDirectionCutoff[i][channel] = light.direction[channel];
      draw.lightColorIntensity[i][channel] = light.color[channel];
      draw.lightAttenuationDrop[i][channel] = light.attenuation[channel];
    }
    draw.lightPositionType[i][3] = static_cast<float>(light.type);
    draw.lightDirectionCutoff[i][3] = std::cos(light.cutOffAngle);
    draw.lightColorIntensity[i][3] = light.intensity;
    draw.lightAttenuationDrop[i][3] = light.dropOffRate;
  }
}

template <typename T>
int compareValue(const T & lhs, const T & rhs)
{
  return lhs < rhs ? -1 : (rhs < lhs ? 1 : 0);
}

int compareBytes(const void * lhs, const void * rhs, size_t bytes)
{
  const int comparison = std::memcmp(lhs, rhs, bytes);
  return comparison < 0 ? -1 : (comparison > 0 ? 1 : 0);
}

int compareDrawGroupingKey(const CoinBgfxDraw & lhs,
                           const CoinBgfxDraw & rhs)
{
  int result = compareValue(lhs.renderLayer, rhs.renderLayer);
  if (result == 0) result = compareValue(lhs.cullMode, rhs.cullMode);
  if (result == 0) result = compareValue(lhs.frontFace, rhs.frontFace);
  if (result == 0) result = compareValue(lhs.depthTest, rhs.depthTest);
  if (result == 0) result = compareValue(lhs.depthWrite, rhs.depthWrite);
  if (result == 0) result = compareValue(lhs.depthFunction, rhs.depthFunction);
  if (result == 0) result = compareBytes(lhs.depthRange, rhs.depthRange, sizeof(lhs.depthRange));
  if (result == 0) result = compareValue(lhs.polygonOffsetFactor, rhs.polygonOffsetFactor);
  if (result == 0) result = compareValue(lhs.polygonOffsetUnits, rhs.polygonOffsetUnits);
  if (result == 0) result = compareValue(lhs.polygonOffsetSlopeBias, rhs.polygonOffsetSlopeBias);
  if (result == 0) result = compareBytes(lhs.clipMeta, rhs.clipMeta, sizeof(lhs.clipMeta));
  if (result == 0) result = compareBytes(lhs.clipPlanes, rhs.clipPlanes, sizeof(lhs.clipPlanes));
  if (result == 0) result = compareBytes(lhs.viewport, rhs.viewport, sizeof(lhs.viewport));
  if (result == 0) result = compareValue(lhs.materialSignature, rhs.materialSignature);
  if (result == 0) result = compareBytes(lhs.fogColorMode, rhs.fogColorMode, sizeof(lhs.fogColorMode));
  if (result == 0) result = compareBytes(lhs.fogRange, rhs.fogRange, sizeof(lhs.fogRange));
  if (result == 0) result = compareBytes(lhs.extraTextures, rhs.extraTextures, sizeof(lhs.extraTextures));
  if (result == 0) result = compareBytes(lhs.textureCombines, rhs.textureCombines, sizeof(lhs.textureCombines));
  if (result == 0) result = compareValue(lhs.hasTexture, rhs.hasTexture);
  if (result == 0) result = compareValue(lhs.textureSlot, rhs.textureSlot);
  if (result == 0) result = compareValue(lhs.textureModel, rhs.textureModel);
  if (result == 0) result = compareValue(lhs.wrapS, rhs.wrapS);
  if (result == 0) result = compareValue(lhs.wrapT, rhs.wrapT);
  if (result == 0) result = compareValue(lhs.filter, rhs.filter);
  if (result == 0) result = compareBytes(lhs.textureBlendColor,
    rhs.textureBlendColor, sizeof(lhs.textureBlendColor));
  if (result == 0) result = compareBytes(lhs.ambientLight,
    rhs.ambientLight, sizeof(lhs.ambientLight));
  if (result == 0) result = compareBytes(lhs.lightCount,
    rhs.lightCount, sizeof(lhs.lightCount));
  if (result == 0) result = compareBytes(lhs.lightPositionType,
    rhs.lightPositionType, sizeof(lhs.lightPositionType));
  if (result == 0) result = compareBytes(lhs.lightDirectionCutoff,
    rhs.lightDirectionCutoff, sizeof(lhs.lightDirectionCutoff));
  if (result == 0) result = compareBytes(lhs.lightColorIntensity,
    rhs.lightColorIntensity, sizeof(lhs.lightColorIntensity));
  if (result == 0) result = compareBytes(lhs.lightAttenuationDrop,
    rhs.lightAttenuationDrop, sizeof(lhs.lightAttenuationDrop));
  return result;
}

bool sameTexture(const CoinBgfxTexture & lhs, const CoinBgfxTexture & rhs)
{
  return lhs.width == rhs.width && lhs.height == rhs.height &&
    lhs.gpuToken == rhs.gpuToken &&
    lhs.pixelsRgba == rhs.pixelsRgba;
}

bool opaqueDrawCanBeGrouped(const CoinBgfxDraw & draw)
{
  // Reordering is only retained for Coin's ordinary opaque depth contract.
  // Non-default comparisons, disabled test/write, remapped depth ranges and
  // polygon offset make overlapping draws order-sensitive. In those cases a
  // pipeline/material sort can visibly differ from sequential GL traversal.
  if (draw.blend && !draw.deferred) return false;
  return draw.blend ||
    (draw.depthTest && draw.depthWrite &&
     draw.depthFunction == CoinRenderDepthFunction::LESS &&
     draw.depthRange[0] == 0.0f && draw.depthRange[1] == 1.0f &&
     draw.polygonOffsetFactor == 0.0f && draw.polygonOffsetUnits == 0.0f &&
     draw.polygonOffsetSlopeBias == 0.0f);
}

bool sameDrawExceptMaterial(const CoinBgfxDraw & lhs,
                            const CoinBgfxDraw & rhs)
{
  CoinBgfxDraw a = lhs;
  CoinBgfxDraw b = rhs;
  a.alpha = b.alpha = 0.0f;
  a.materialSignature = b.materialSignature = 0;
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool sameVertexExceptMaterial(const CoinBgfxVertex & lhs,
                              const CoinBgfxVertex & rhs)
{
  CoinBgfxVertex a = lhs;
  CoinBgfxVertex b = rhs;
  std::memset(a.color, 0, sizeof(a.color));
  std::memset(b.color, 0, sizeof(b.color));
  std::memset(a.ambient, 0, sizeof(a.ambient));
  std::memset(b.ambient, 0, sizeof(b.ambient));
  std::memset(a.specular, 0, sizeof(a.specular));
  std::memset(b.specular, 0, sizeof(b.specular));
  std::memset(a.emission, 0, sizeof(a.emission));
  std::memset(b.emission, 0, sizeof(b.emission));
  a.material[0] = b.material[0] = 0.0f;
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

}

bool
CoinBgfxLowering::retainForReuse(CoinBgfxPlan & plan,
                               uint64_t geometryBudget, uint64_t metadataBudget)
{
  uint64_t metadataBytes = uint64_t(plan.draws.capacity() + plan.shadowDraws.capacity()) * sizeof(CoinBgfxDraw) +
    uint64_t(plan.textures.capacity()) * sizeof(CoinBgfxTexture) +
    uint64_t(plan.instances.capacity()) * sizeof(CoinBgfxInstance);
  for (const auto & texture : plan.textures) metadataBytes += texture.pixelsRgba.capacity();
  if (metadataBytes > metadataBudget) return false;
  // Large uploads can transfer the vertex allocation to BGFX before caching.
  // Keep their count, and also make retaining an already released plan safe.
  plan.uploadedVertexCount = plan.vertexCount();
  if (!plan.indices.empty()) plan.uploadedIndexCount = plan.indices.size();
  // Account for transferred vertices as well, so their remaining indices do
  // not accidentally become eligible for the small-plan material cache.
  const uint64_t geometryBytes = (plan.usesInstancing
    ? uint64_t(std::max(plan.instancedVertices.capacity(), plan.uploadedVertexCount)) * sizeof(CoinBgfxInstancedVertex)
    : plan.usesCompactVertices
    ? uint64_t(std::max(plan.packedVertices.capacity(), plan.uploadedVertexCount)) * sizeof(CoinBgfxVertexPrefix)
    : uint64_t(std::max(plan.vertices.capacity(), plan.uploadedVertexCount)) * sizeof(CoinBgfxVertex)) +
    uint64_t(std::max(plan.indices.capacity(), plan.uploadedIndexCount)) * sizeof(uint32_t);
  if (plan.usesCompactVertices || geometryBytes + metadataBytes > geometryBudget) {
    std::vector<CoinBgfxVertex>().swap(plan.vertices);
    std::vector<CoinBgfxVertexPrefix>().swap(plan.packedVertices);
    std::vector<CoinBgfxInstancedVertex>().swap(plan.instancedVertices);
    std::vector<uint32_t>().swap(plan.indices);
  }
  return true;
}

bool
CoinBgfxLowering::clipViewport(const int32_t viewport[4], int width, int height,
                            int32_t clipped[4])
{
  return CoinRenderTransformCore::clipViewport(viewport, width, height, clipped);
}

bool
CoinBgfxLowering::lowerInstanced(const CoinRenderFramePlan & frame, int width, int height,
                               bool homogeneousDepth, CoinBgfxPlan & output,
                               std::string & diagnostic,
                               const CoinRenderFramePreflight * preflight)
{
  diagnostic.clear();
  const auto decline = [&](const std::string & reason) {
    diagnostic = "BGFX instancing declined: " + reason;
    return false;
  };
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return decline("target size");
  if (frame.draws.size() < 256 || frame.draws.size() > UINT32_MAX) return decline("draw count");
  if (!frame.shadowGroups.empty()) return decline("shadow groups");
  if (!frame.textures.empty()) return decline("texture images");
  if ((!preflight || !preflight->compositionFor(frame)) && !frame.isValid(&diagnostic)) return false;
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_schedule(frame, order, diagnostic, preflight)) return false;
  if (order.size() != frame.draws.size()) return decline("composition count");
  const auto & firstState = frame.renderStates[frame.draws[0].renderStateSlot];
  const auto view = firstState.view.getValue();
  if (view[0][3] != 0.0f || view[1][3] != 0.0f || view[2][3] != 0.0f || view[3][3] != 1.0f) return decline("projective view");
  const auto & viewport = frame.viewports[firstState.viewportSlot];
  const SbMatrix projection = CoinRenderTransformCore::projection(firstState.projectionCoin, homogeneousDepth);
  const SbMatrix mvp = projection * CoinRenderTransformCore::viewportTransform(viewport, width, height);
  if (!instancedMatrixMagnitude(mvp)) return decline("projection coefficient magnitude");

  CoinBgfxDraw drawTemplate{};
  std::memcpy(drawTemplate.mvp, mvp.getValue(), sizeof(drawTemplate.mvp));
  drawTemplate.cullMode = firstState.cullMode;
  drawTemplate.frontFace = firstState.frontFace;
  drawTemplate.depthFunction = firstState.depthFunction;
  drawTemplate.alpha = 1.0f;
  drawTemplate.viewport[0] = viewport.x; drawTemplate.viewport[1] = viewport.y;
  drawTemplate.viewport[2] = viewport.width; drawTemplate.viewport[3] = viewport.height;
  const auto & lighting = frame.lightingStates[firstState.lightingSlot];
  copyLighting(lighting, drawTemplate);

  struct Mesh { uint32_t firstVertex, vertexCount, firstIndex, indexCount; };
  struct Range { uint32_t meshSlot, materialSlot; float positionScale[3]; };
  // New span metadata is bounded independently of the existing packed/GPU
  // buffers. Logical accounting includes each key/value, hash node/bucket
  // allowance and mesh lookup entries; no per-occurrence normalization array
  // is retained. 65536 spans also bounds hashing-table allocation growth.
  constexpr size_t maxSourceSpans = 65536;
  constexpr uint64_t metadataBudget = 8u * 1024u * 1024u;
  constexpr uint64_t spanMetadataBytes = sizeof(SharedRangeKey) + sizeof(Range) + 6 * sizeof(void *);
  constexpr uint64_t meshMetadataBytes = sizeof(Mesh) + sizeof(uint64_t) + sizeof(uint32_t) + 8 * sizeof(void *);
  uint64_t metadataBytes = 0;
  std::vector<Mesh> meshes;
  std::unordered_map<uint64_t, std::vector<uint32_t>> meshByHash;
  std::unordered_map<SharedRangeKey, Range, SharedRangeHash> ranges;
  std::vector<uint32_t> remap;
  CoinBgfxPlan candidate;
  candidate.usesInstancing = true;
  candidate.instanceCameraAnchorView = firstState.view;
  candidate.instancedCameraPatchable = instancedMatrixMagnitude(firstState.view) &&
    CoinRenderTransformCore::cameraReuseView(firstState.view);
  candidate.instances.reserve(frame.draws.size());
  candidate.draws.reserve(std::min<size_t>(frame.draws.size(), 256));
  uint32_t previousMesh = UINT32_MAX;
  for (size_t ordinal = 0; ordinal < order.size(); ++ordinal) {
    const auto & item = order[ordinal];
    const auto & sourceDraw = frame.draws[item.drawIndex];
    const auto & state = frame.renderStates[sourceDraw.renderStateSlot];
    const auto & geometry = sourceDraw.geometry;
    const auto declineDraw = [&](const std::string & reason) {
      return decline("draw " + std::to_string(item.drawIndex) + ": " + reason);
    };
    if (item.drawIndex != ordinal || item.firstIndex != geometry.firstIndex ||
        item.indexCount != geometry.indexCount) return declineDraw("composition order/range");
    if (item.blend || (item.screenDoor && item.screenDoorLevel != 0) || item.sortTriangles || item.additive)
      return declineDraw("composition blend=" + std::to_string(item.blend) +
                         " screenDoor=" + std::to_string(item.screenDoorLevel) +
                         " sortTriangles=" + std::to_string(item.sortTriangles) +
                         " additive=" + std::to_string(item.additive));
    if (sourceDraw.shadowLightSlot || sourceDraw.renderLayer != 0 || sourceDraw.clearDepthBefore ||
        sourceDraw.lineStripId || sourceDraw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST)
      return declineDraw("special draw/topology");
    if (state.lightModel != CoinRenderLightModel::PHONG || state.hasTexture ||
        state.transparentMaterial || state.transparentTexture)
      return declineDraw("lightModel=" + std::to_string(static_cast<uint32_t>(state.lightModel)) +
                         " hasTexture=" + std::to_string(state.hasTexture) +
                         " transparentMaterial=" + std::to_string(state.transparentMaterial) +
                         " transparentTexture=" + std::to_string(state.transparentTexture));
    if (!state.clipPlanesWorld.empty() || state.fogMode != CoinRenderFogMode::NONE ||
        state.shadowGroupSlot || state.polygonOffsetEnabled || state.polygonLinePattern || state.linePattern != 0xffffu)
      return declineDraw("surface clip=" + std::to_string(state.clipPlanesWorld.size()) +
                         " fog=" + std::to_string(static_cast<uint32_t>(state.fogMode)) +
                         " shadow=" + std::to_string(state.shadowGroupSlot) +
                         " offset=" + std::to_string(state.polygonOffsetEnabled) +
                         " polygonPattern=" + std::to_string(state.polygonLinePattern) +
                         " linePattern=" + std::to_string(state.linePattern));
    if (state.explicitDepthMask || !item.depthTest || !item.depthWrite ||
        (item.depthFunction != CoinRenderDepthFunction::LESS && item.depthFunction != CoinRenderDepthFunction::LEQUAL) ||
        item.depthFunction != drawTemplate.depthFunction ||
        item.depthRange[0] != 0.0f || item.depthRange[1] != 1.0f)
      return declineDraw("depth mask=" + std::to_string(state.explicitDepthMask) +
                         " test=" + std::to_string(item.depthTest) + " write=" + std::to_string(item.depthWrite) +
                         " function=" + std::to_string(static_cast<uint32_t>(item.depthFunction)) +
                         " range=" + std::to_string(item.depthRange[0]) + "," + std::to_string(item.depthRange[1]));
    if (state.cullMode != firstState.cullMode || state.frontFace != firstState.frontFace)
      return declineDraw("different face/cull state");
    if (state.viewportSlot != firstState.viewportSlot) return declineDraw("different viewport slot");
    if (state.lightingSlot != firstState.lightingSlot) return declineDraw("different lighting slot");
    if (std::memcmp(state.view.getValue(), firstState.view.getValue(), sizeof(SbMat)) != 0)
      return declineDraw("different view");
    if (std::memcmp(state.projectionCoin.getValue(), firstState.projectionCoin.getValue(), sizeof(SbMat)) != 0)
      return declineDraw("different projection");
    if (!geometry.vertexCount || geometry.vertexCount > 4096 ||
        !geometry.indexCount || geometry.indexCount > 65536) return declineDraw("mesh range size");
    if ((state.cullMode != CoinRenderCullMode::NONE && state.cullMode != CoinRenderCullMode::BACK &&
         state.cullMode != CoinRenderCullMode::FRONT) ||
        (state.frontFace != CoinRenderFrontFace::CCW && state.frontFace != CoinRenderFrontFace::CW)) return declineDraw("unknown face/cull");
    for (const auto & texture : state.extraTextures) if (texture.enabled) return declineDraw("extra texture unit");
    const auto model = state.model.getValue();
    if (model[0][3] != 0.0f || model[1][3] != 0.0f || model[2][3] != 0.0f || model[3][3] != 1.0f) return declineDraw("projective model");
    const SbMatrix modelView = state.model * state.view;
    // normalMatrix uses an identity fallback at |det| <= 1e-12. A camera
    // rotation cannot compose that fallback, so only cache a patchable anchor
    // well away from the boundary. Full lowering still supports these models.
    const auto mv = modelView.getValue();
    if (!instancedMatrixMagnitude(modelView) || mv[0][3] != 0.0f ||
        mv[1][3] != 0.0f || mv[2][3] != 0.0f || mv[3][3] != 1.0f) return declineDraw("model-view coefficients/affinity");
    const SbMatrix normalMatrix = CoinRenderTransformCore::normalMatrix(modelView);
    if (!instancedMatrixMagnitude(normalMatrix)) return declineDraw("normal matrix coefficient magnitude");
    candidate.instancedCameraPatchable = candidate.instancedCameraPatchable &&
      composableInstanceNormal(modelView, normalMatrix);

    const SharedRangeKey key = {geometry.firstVertex, geometry.vertexCount,
                               geometry.firstIndex, geometry.indexCount};
    auto range = ranges.find(key);
    if (range == ranges.end()) {
      if (ranges.size() >= maxSourceSpans || metadataBytes + spanMetadataBytes > metadataBudget)
        return declineDraw("source span/metadata budget");
      std::vector<CoinBgfxInstancedVertex> vertices;
      std::vector<uint32_t> indices;
      vertices.reserve(std::min(geometry.vertexCount, geometry.indexCount));
      indices.reserve(geometry.indexCount);
      remap.assign(geometry.vertexCount, UINT32_MAX);
      const uint32_t materialSlot = frame.vertices[frame.indices[geometry.firstIndex]].materialSlot;
      const auto & material = frame.materials[materialSlot];
      if (material.diffuse[3] != 1.0f || material.transparency != 0.0f || !std::isfinite(material.shininess)) return declineDraw("material alpha/shininess");
      for (int channel = 0; channel < 4; ++channel)
        if (!std::isfinite(material.diffuse[channel]) || !std::isfinite(material.ambient[channel]) ||
            !std::isfinite(material.specular[channel]) || !std::isfinite(material.emission[channel])) return declineDraw("non-finite material");
      for (uint32_t j = 0; j < geometry.indexCount; ++j) {
        const uint32_t sourceIndex = frame.indices[geometry.firstIndex + j];
        if (sourceIndex < geometry.firstVertex || sourceIndex >= geometry.firstVertex + geometry.vertexCount) return declineDraw("index escapes vertex range");
        const auto & source = frame.vertices[sourceIndex];
        if (source.materialSlot != materialSlot) return declineDraw("nonuniform vertex material");
        if (source.screenSpaceW != 1.0f || source.fogEyeDepth >= 0.0f) return declineDraw("stroke interpolation/fog depth");
        for (int channel = 0; channel < 3; ++channel)
          if (!instancedMagnitude(source.position[channel]) || !instancedMagnitude(source.normal[channel])) return declineDraw("source position/normal magnitude");
        uint32_t & slot = remap[sourceIndex - geometry.firstVertex];
        if (slot == UINT32_MAX) {
          CoinBgfxInstancedVertex vertex;
          std::memcpy(vertex.position, source.position, sizeof(vertex.position));
          std::memcpy(vertex.normal, source.normal, sizeof(vertex.normal));
          slot = static_cast<uint32_t>(vertices.size());
          vertices.push_back(vertex);
        }
        indices.push_back(slot);
      }
      float positionScale[3] = {1.0f, 1.0f, 1.0f};
      normalizeDiagonalMesh(vertices, positionScale);
      uint32_t meshSlot = UINT32_MAX;
      const auto sameMesh = [&](uint32_t slot) {
        const auto & mesh = meshes[slot];
        if (mesh.vertexCount != vertices.size() || mesh.indexCount != indices.size() ||
            std::memcmp(candidate.instancedVertices.data() + mesh.firstVertex, vertices.data(),
                        vertices.size() * sizeof(CoinBgfxInstancedVertex)) != 0) return false;
        for (size_t j = 0; j < indices.size(); ++j)
          if (candidate.indices[mesh.firstIndex + j] != mesh.firstVertex + indices[j]) return false;
        return true;
      };
      // Many distinct source spans have exactly the same normalized mesh.
      // Prove equality against the common first/previous mesh before running a
      // byte-wise FNV pass; mutable input spans are still read on every call.
      if (!meshes.empty() && sameMesh(0)) meshSlot = 0;
      else if (previousMesh != UINT32_MAX && previousMesh != 0 && sameMesh(previousMesh)) meshSlot = previousMesh;
      if (meshSlot == UINT32_MAX) {
        uint64_t hash = hashBytes(UINT64_C(1469598103934665603), vertices.data(), vertices.size() * sizeof(CoinBgfxInstancedVertex));
        hash = hashBytes(hash, indices.data(), indices.size() * sizeof(uint32_t));
        auto & matches = meshByHash[hash];
        for (uint32_t slot : matches)
          if (sameMesh(slot)) { meshSlot = slot; break; }
        if (meshSlot == UINT32_MAX) {
          if (meshes.size() >= 256 || metadataBytes + spanMetadataBytes + meshMetadataBytes > metadataBudget)
            return declineDraw("distinct mesh/metadata budget");
          meshSlot = static_cast<uint32_t>(meshes.size());
          Mesh mesh = {static_cast<uint32_t>(candidate.instancedVertices.size()), static_cast<uint32_t>(vertices.size()),
                       static_cast<uint32_t>(candidate.indices.size()), static_cast<uint32_t>(indices.size())};
          candidate.instancedVertices.insert(candidate.instancedVertices.end(), vertices.begin(), vertices.end());
          for (uint32_t index : indices) candidate.indices.push_back(mesh.firstVertex + index);
          meshes.push_back(mesh);
          matches.push_back(meshSlot);
          metadataBytes += meshMetadataBytes;
        }
      }
      range = ranges.emplace(key, Range{meshSlot, materialSlot,
        {positionScale[0], positionScale[1], positionScale[2]}}).first;
      metadataBytes += spanMetadataBytes;
    }
    const auto & mesh = meshes[range->second.meshSlot];
    const auto & material = frame.materials[range->second.materialSlot];
    SbMatrix positionModelView = modelView;
    for (int row = 0; row < 3; ++row)
      for (int column = 0; column < 4; ++column)
        positionModelView[row][column] *= range->second.positionScale[row];
    if (!instancedMatrixMagnitude(positionModelView)) return declineDraw("factored position matrix coefficient magnitude");
    const auto position = positionModelView.getValue();
    CoinBgfxInstance instance{};
    const auto normal = normalMatrix.getValue();
    for (int column = 0; column < 3; ++column) {
      for (int row = 0; row < 3; ++row) {
        instance.data[column][row] = position[row][column];
        instance.data[column + 3][row] = normal[row][column];
      }
      instance.data[column][3] = mv[3][column];
    }
    instance.data[3][3] = material.shininess;
    instance.data[4][3] = 1.0f;
    std::memcpy(instance.data[6], material.diffuse, sizeof(material.diffuse));
    std::memcpy(instance.data[7], material.ambient, sizeof(material.ambient));
    std::memcpy(instance.data[8], material.specular, sizeof(material.specular));
    std::memcpy(instance.data[9], material.emission, sizeof(material.emission));
    const uint32_t instanceSlot = static_cast<uint32_t>(candidate.instances.size());
    candidate.instances.push_back(instance);
    if (previousMesh == range->second.meshSlot && !candidate.draws.empty()) {
      ++candidate.draws.back().instanceCount;
    }
    else {
      CoinBgfxDraw draw = drawTemplate;
      draw.sourceDrawSlot = static_cast<uint32_t>(item.drawIndex);
      draw.renderStateSlot = sourceDraw.renderStateSlot;
      draw.sourceNodeId = sourceDraw.sourceNodeId;
      draw.firstVertex = mesh.firstVertex; draw.vertexCount = mesh.vertexCount;
      draw.firstIndex = mesh.firstIndex; draw.indexCount = mesh.indexCount;
      draw.firstInstance = instanceSlot; draw.instanceCount = 1;
      draw.materialSignature = UINT64_C(1469598103934665603);
      for (uint32_t j = 0; j < geometry.indexCount; ++j) {
        draw.materialSignature = hashBytes(draw.materialSignature, material.diffuse, sizeof(material.diffuse));
        draw.materialSignature = hashBytes(draw.materialSignature, material.ambient, sizeof(material.ambient));
        draw.materialSignature = hashBytes(draw.materialSignature, material.specular, sizeof(material.specular));
        draw.materialSignature = hashBytes(draw.materialSignature, material.emission, sizeof(material.emission));
        draw.materialSignature = hashBytes(draw.materialSignature, &material.shininess, sizeof(material.shininess));
      }
      candidate.draws.push_back(draw);
    }
    previousMesh = range->second.meshSlot;
  }
  for (int channel = 0; channel < 4; ++channel) candidate.clearColor[channel] = frame.clearColor[channel];
  output = std::move(candidate);
  return true;
}

bool
CoinBgfxLowering::lower(const CoinRenderFramePlan & frame, int width, int height,
                      bool homogeneousDepth, CoinBgfxPlan & output,
                      std::string & diagnostic, bool allowQualifiedShadows, bool batchOpaque,
                      const CoinRenderFramePreflight * preflight,
                      bool compactOpaqueVertices)
{
  diagnostic.clear();
  CoinBgfxPlan candidate;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    diagnostic = "BGFX evaluation requires a nonzero target up to 16384 pixels per side";
    return false;
  }
  if (!allowQualifiedShadows && !frame.shadowGroups.empty()) {
    diagnostic = "BGFX shadow maps require a qualified shadow executor";
    return false;
  }
  if ((!preflight || !preflight->compositionFor(frame)) && !frame.isValid(&diagnostic)) return false;
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_schedule(frame, order, diagnostic, preflight))
    return false;

  for (size_t d = 0; d < frame.draws.size(); ++d) if (frame.draws[d].shadowLightSlot) {
    CoinRenderCompositionItem item;
    item.drawIndex = d; item.firstIndex = frame.draws[d].geometry.firstIndex;
    item.indexCount = frame.draws[d].geometry.indexCount;
    order.push_back(item);
  }
  // Bound the scratch remap independently of scene size. Larger ranges retain
  // the expanded path; a remap is local to one composition item, never a draw
  // with another transform/material or a differently ordered transparent item.
  constexpr uint32_t indexedRangeLimit = 4096;
  std::vector<uint32_t> vertexRemap;
  uint64_t vertexBudget = 0, indexBudget = 0;
  bool compact = compactOpaqueVertices;
  for (const auto & item : order) {
    const auto & packet = frame.draws[item.drawIndex];
    const auto & geometry = packet.geometry;
    const bool opaqueCandidate = batchOpaque && frame.draws.size() >= 256 && frame.shadowGroups.empty() &&
      !item.blend && !item.screenDoor && packet.renderLayer == 0 && !packet.clearDepthBefore &&
      frame.renderStates[packet.renderStateSlot].lightModel == CoinRenderLightModel::PHONG;
    vertexBudget += opaqueCandidate && geometry.vertexCount <= indexedRangeLimit
      ? std::min(geometry.vertexCount, item.indexCount) : item.indexCount;
    indexBudget += item.indexCount;
    if (compact) {
      const auto & state = frame.renderStates[packet.renderStateSlot];
      if (!opaqueCandidate || geometry.vertexCount > indexedRangeLimit || state.hasTexture)
        compact = false;
      for (const auto & layer : state.extraTextures) if (layer.enabled) compact = false;
      for (int axis = 0; axis < 3; ++axis)
        if (state.model[axis][3] != 0.0f || state.view[axis][3] != 0.0f) compact = false;
      if (state.model[3][3] != 1.0f || state.view[3][3] != 1.0f) compact = false;
    }
  }
  compact = compact && vertexBudget * sizeof(CoinBgfxVertex) > 32u * 1024u * 1024u;
  if (compact) for (const auto & vertex : frame.vertices)
    if (vertex.screenSpaceW != 1.0f || vertex.fogEyeDepth >= 0.0f) { compact = false; break; }
  candidate.usesCompactVertices = compact;
  if (vertexBudget <= UINT32_MAX / sizeof(CoinBgfxVertex)) {
    if (compact) candidate.packedVertices.reserve(static_cast<size_t>(vertexBudget));
    else candidate.vertices.reserve(static_cast<size_t>(vertexBudget));
  }
  if (indexBudget <= UINT32_MAX / sizeof(uint32_t))
    candidate.indices.reserve(static_cast<size_t>(indexBudget));
  candidate.draws.reserve(batchOpaque ? std::min<size_t>(order.size(), 256) : order.size());
  // Preserve the exact legacy FNV sequence for uniform-material geometry.
  // Repeated shapes otherwise hash the same material once per vertex.
  std::unordered_map<uint64_t, uint64_t> uniformMaterialSignatures;
  std::unordered_map<SharedRangeKey, SharedRange, SharedRangeHash> sharedRanges;
  constexpr size_t sharedRangeLimit = 256;
  constexpr size_t sharedIndexBudget = 1024u * 1024u;
  size_t sharedIndexCount = 0;
  const char * disableSharedRanges = std::getenv("COIN_BGFX_DISABLE_SHARED_RANGE_LOWERING");
  const bool useSharedRanges = !disableSharedRanges || std::strcmp(disableSharedRanges, "1") != 0;
  std::vector<uint8_t> validatedMaterialColors(frame.materials.size(), 0);
  bool previousBatchable = false;
  for (const CoinRenderCompositionItem& item : order) {
    CoinRenderDrawPacket draw = frame.draws[item.drawIndex];
    draw.geometry.firstIndex = item.firstIndex;
    draw.geometry.indexCount = item.indexCount;
    const CoinRenderRenderStateSnapshot& state = frame.renderStates[draw.renderStateSlot];
    const CoinRenderViewportSnapshot & viewport = frame.viewports[state.viewportSlot];
    if ((state.cullMode != CoinRenderCullMode::NONE && state.cullMode != CoinRenderCullMode::BACK &&
         state.cullMode != CoinRenderCullMode::FRONT) ||
        (state.frontFace != CoinRenderFrontFace::CCW && state.frontFace != CoinRenderFrontFace::CW)) {
      diagnostic = "BGFX evaluation received an unknown face/cull state";
      return false;
    }
    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST) {
      diagnostic = "BGFX evaluation supports PHONG or BASE_COLOR triangles with fog";
      return false;
    }
    if (state.hasTexture) {
      if (state.textureImageSlot >= frame.textures.size() ||
          state.samplerSlot >= frame.samplers.size()) {
        diagnostic = "BGFX textured draw references an invalid image or sampler";
        return false;
      }
      const CoinRenderTextureImageSnapshot & texture = frame.textures[state.textureImageSlot];
      const uint64_t expected = static_cast<uint64_t>(texture.width) * texture.height * 4u;
      if (texture.width == 0 || texture.height == 0 ||
          (texture.gpuToken == 0 && texture.pixelsRgba.size() != expected) ||
          (texture.gpuToken != 0 && !texture.pixelsRgba.empty())) {
        diagnostic = "BGFX requires canonical RGBA8 pixels or a direct GPU texture token";
        return false;
      }
    }
    if (draw.geometry.indexCount == 0 || draw.geometry.vertexCount == 0) {
      diagnostic = "BGFX evaluation requires nonempty indexed triangles";
      return false;
    }
    bool batchable = batchOpaque && frame.draws.size() >= 256 && frame.shadowGroups.empty() &&
      !item.blend && !item.screenDoor && draw.renderLayer == 0 && !draw.clearDepthBefore &&
      state.lightModel == CoinRenderLightModel::PHONG;
    float maxAlpha = 0.0f;
    uint32_t uniformMaterialSlot = frame.vertices[frame.indices[draw.geometry.firstIndex]].materialSlot;
    bool uniformMaterial = true;
    const SharedRangeKey rangeKey = {draw.geometry.firstVertex, draw.geometry.vertexCount,
                                    draw.geometry.firstIndex, draw.geometry.indexCount};
    const SharedRange * sharedRange = nullptr;
    if (useSharedRanges && batchable && draw.geometry.vertexCount <= indexedRangeLimit) {
      const auto cached = sharedRanges.find(rangeKey);
      if (cached != sharedRanges.end()) sharedRange = &cached->second;
    }
    if (sharedRange) {
      uniformMaterialSlot = sharedRange->uniformMaterialSlot;
      uniformMaterial = sharedRange->uniformMaterial;
      maxAlpha = sharedRange->maxAlpha;
    }
    else for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t index = frame.indices[draw.geometry.firstIndex + j];
      if (index < draw.geometry.firstVertex ||
          index >= draw.geometry.firstVertex + draw.geometry.vertexCount) {
        diagnostic = "BGFX draw index escapes its vertex range";
        return false;
      }
      const CoinRenderMaterialSnapshot & material = frame.materials[frame.vertices[index].materialSlot];
      const auto & vertex = frame.vertices[index];
      if (vertex.materialSlot != uniformMaterialSlot) uniformMaterial = false;
      if (vertex.screenSpaceW != 1.0f || vertex.fogEyeDepth >= 0.0f) batchable = false;
      if (!validatedMaterialColors[vertex.materialSlot]) {
        if (!coin_render_is_finite(material.diffuse[0]) || !coin_render_is_finite(material.diffuse[1]) ||
            !coin_render_is_finite(material.diffuse[2]) || !coin_render_is_finite(material.diffuse[3])) {
          diagnostic = "BGFX evaluation received non-finite material color";
          return false;
        }
        validatedMaterialColors[vertex.materialSlot] = 1;
      }
      if (material.diffuse[3] > maxAlpha) maxAlpha = material.diffuse[3];
    }
    const SbMatrix modelView = state.model * state.view;
    // Baking view-space positions preserves perspective interpolation only
    // for affine transforms. Strokes and shadow passes keep their own path.
    for (int axis = 0; axis < 3; ++axis)
      if (modelView[axis][3] != 0.0f) batchable = false;
    if (modelView[3][3] != 1.0f) batchable = false;
    const SbMatrix normalMatrix = CoinRenderTransformCore::normalMatrix(modelView);
    const SbMatrix projection = CoinRenderTransformCore::projection(state.projectionCoin, homogeneousDepth);
    const SbMatrix viewportTransform = CoinRenderTransformCore::viewportTransform(viewport, width, height);
    const SbMatrix mvp = batchable ? projection * viewportTransform :
      modelView * projection * viewportTransform;
    CoinBgfxDraw lowered{};
    lowered.sourceDrawSlot = item.drawIndex;
    lowered.renderStateSlot = draw.renderStateSlot;
    std::memcpy(lowered.mvp, mvp.getValue(), sizeof(lowered.mvp));
    lowered.firstVertex = static_cast<uint32_t>(candidate.vertexCount());
    lowered.vertexCount = 0;
    lowered.firstIndex = static_cast<uint32_t>(candidate.indices.size());
    lowered.indexCount = draw.geometry.indexCount;
    lowered.cullMode = state.cullMode;
    lowered.frontFace = state.frontFace;
    // The current layer compositor publishes color over the opaque depth.
    // Reject effective states this mechanism cannot preserve before submission.
    if (item.blend && item.deferred &&
        item.transparencyStrategy == CoinRenderCompositionItem::SORTED_LAYERS &&
        (item.depthWrite || !item.depthTest ||
         (item.depthFunction != CoinRenderDepthFunction::LESS &&
          item.depthFunction != CoinRenderDepthFunction::LEQUAL &&
          item.depthFunction != CoinRenderDepthFunction::NEVER))) {
      diagnostic = "BGFX sorted layers requires depth test LESS/LEQUAL/NEVER and no transparent depth writes";
      return false;
    }
    lowered.depthTest = item.depthTest;
    lowered.depthWrite = item.depthWrite;
    lowered.depthFunction = item.depthFunction;
    lowered.depthRange[0] = item.depthRange[0];
    lowered.depthRange[1] = item.depthRange[1];
    if (state.polygonOffsetEnabled &&
        (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) != 0) {
      lowered.polygonOffsetFactor = state.polygonOffsetFactor;
      lowered.polygonOffsetUnits = state.polygonOffsetUnits;
      lowered.polygonOffsetSlopeBias = state.polygonOffsetSlopeBias;
    }
    lowered.blend = item.blend;
    lowered.sourceNodeId = draw.sourceNodeId;
    lowered.sortTriangles = item.sortTriangles;
    lowered.deferred = item.deferred;
    lowered.additive = item.additive;
    lowered.clipMeta[0] = static_cast<float>(state.clipPlanesWorld.size());
    if (!coin_render_clip_equations(state, lowered.clipPlanes, diagnostic)) return false;
    lowered.screenDoor[0] = static_cast<float>(item.screenDoorLevel);
    lowered.screenDoor[3] = item.screenDoor ? 1.0f : 0.0f;

    switch (item.transparencyStrategy) {
    case CoinRenderCompositionItem::WEIGHTED_OIT:
      lowered.transparencyStrategy = CoinBgfxTransparencyStrategy::WEIGHTED_OIT; break;
    case CoinRenderCompositionItem::SORTED_LAYERS:
      lowered.transparencyStrategy = CoinBgfxTransparencyStrategy::SORTED_LAYERS; break;
    default:
      lowered.transparencyStrategy = CoinBgfxTransparencyStrategy::OBJECT; break;
    }
    lowered.alpha = maxAlpha;
    lowered.viewport[0] = viewport.x;
    lowered.viewport[1] = viewport.y;
    lowered.viewport[2] = viewport.width;
    lowered.viewport[3] = viewport.height;
    lowered.renderLayer = draw.renderLayer;
    lowered.clearDepthBefore = draw.clearDepthBefore;
    std::memcpy(lowered.textureCombines, state.textureCombines, sizeof(lowered.textureCombines));
    lowered.hasTexture = state.hasTexture;
    lowered.textureSlot = state.textureImageSlot;
    lowered.textureModel = state.textureModel;
    std::memcpy(lowered.textureBlendColor, state.textureBlendColor,
                sizeof(lowered.textureBlendColor));
    if (state.hasTexture) {
      const CoinRenderSamplerSnapshot & sampler = frame.samplers[state.samplerSlot];
      lowered.wrapS = sampler.wrapS;
      lowered.wrapT = sampler.wrapT;
      lowered.filter = sampler.filter;
    }
    for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
      const auto & source = state.extraTextures[unit - 1];
      auto & layer = lowered.extraTextures[unit - 1];
      layer.enabled = source.enabled;
      if (!source.enabled) continue;
      layer.slot = source.imageSlot; layer.model = source.model;
      std::memcpy(layer.blendColor, source.blendColor, sizeof(layer.blendColor));
      const auto & sampler = frame.samplers[source.samplerSlot];
      layer.wrapS = sampler.wrapS; layer.wrapT = sampler.wrapT; layer.filter = sampler.filter;
    }
    std::memcpy(lowered.fogColorMode, state.fogColor, sizeof(state.fogColor));
    lowered.fogColorMode[3] = static_cast<float>(state.fogMode);
    lowered.fogRange[0] = state.fogStart; lowered.fogRange[1] = state.fogEnd;
    const CoinRenderLightingSnapshot & lighting = frame.lightingStates[state.lightingSlot];
    lowered.ambientLight[0] = lighting.ambientColor[0];
    lowered.ambientLight[1] = lighting.ambientColor[1];
    lowered.ambientLight[2] = lighting.ambientColor[2];
    lowered.ambientLight[3] = lighting.ambientIntensity;
    lowered.lightCount[0] = static_cast<float>(lighting.lights.size());
    for (size_t lightIndex = 0; lightIndex < lighting.lights.size(); ++lightIndex) {
      const CoinRenderLightSourceSnapshot & light = lighting.lights[lightIndex];
      for (int channel = 0; channel < 3; ++channel) {
        lowered.lightPositionType[lightIndex][channel] = light.position[channel];
        lowered.lightDirectionCutoff[lightIndex][channel] = light.direction[channel];
        lowered.lightColorIntensity[lightIndex][channel] = light.color[channel];
        lowered.lightAttenuationDrop[lightIndex][channel] = light.attenuation[channel];
      }
      lowered.lightPositionType[lightIndex][3] = static_cast<float>(light.type);
      lowered.lightDirectionCutoff[lightIndex][3] = std::cos(light.cutOffAngle);
      lowered.lightColorIntensity[lightIndex][3] = light.intensity;
      lowered.lightAttenuationDrop[lightIndex][3] = light.dropOffRate;
    }
    lowered.materialSignature = UINT64_C(1469598103934665603);
    if (uniformMaterial) {
      const uint64_t key = (uint64_t(uniformMaterialSlot) << 32) | draw.geometry.indexCount;
      const auto cached = uniformMaterialSignatures.find(key);
      if (cached != uniformMaterialSignatures.end()) lowered.materialSignature = cached->second;
      else {
        const auto & material = frame.materials[uniformMaterialSlot];
        for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
          lowered.materialSignature = hashBytes(lowered.materialSignature, material.diffuse, sizeof(material.diffuse));
          lowered.materialSignature = hashBytes(lowered.materialSignature, material.ambient, sizeof(material.ambient));
          lowered.materialSignature = hashBytes(lowered.materialSignature, material.specular, sizeof(material.specular));
          lowered.materialSignature = hashBytes(lowered.materialSignature, material.emission, sizeof(material.emission));
          lowered.materialSignature = hashBytes(lowered.materialSignature, &material.shininess, sizeof(material.shininess));
        }
        uniformMaterialSignatures.emplace(key, lowered.materialSignature);
      }
    }
    // Qualify indexed conversion with the existing opaque batching guard.
    // Small/unbatched, transparent, shadow and stroke paths keep their legacy
    // expansion, including the established driver behavior of those profiles.
    const bool indexed = batchable && draw.geometry.vertexCount <= indexedRangeLimit;
    if (useSharedRanges && indexed && !sharedRange && sharedRanges.size() < sharedRangeLimit &&
        draw.geometry.indexCount <= sharedIndexBudget - sharedIndexCount) {
      SharedRange range;
      range.uniformMaterialSlot = uniformMaterialSlot;
      range.uniformMaterial = uniformMaterial;
      range.maxAlpha = maxAlpha;
      range.materialSignature = lowered.materialSignature;
      range.sourceVertices.reserve(std::min(draw.geometry.vertexCount, draw.geometry.indexCount));
      range.relativeIndices.reserve(draw.geometry.indexCount);
      vertexRemap.assign(draw.geometry.vertexCount, UINT32_MAX);
      for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
        const uint32_t sourceIndex = frame.indices[draw.geometry.firstIndex + j];
        uint32_t & slot = vertexRemap[sourceIndex - draw.geometry.firstVertex];
        if (slot == UINT32_MAX) {
          slot = static_cast<uint32_t>(range.sourceVertices.size());
          range.sourceVertices.push_back(sourceIndex);
        }
        range.relativeIndices.push_back(slot);
        if (!uniformMaterial) {
          const auto & material = frame.materials[frame.vertices[sourceIndex].materialSlot];
          range.materialSignature = hashBytes(range.materialSignature, material.diffuse, sizeof(material.diffuse));
          range.materialSignature = hashBytes(range.materialSignature, material.ambient, sizeof(material.ambient));
          range.materialSignature = hashBytes(range.materialSignature, material.specular, sizeof(material.specular));
          range.materialSignature = hashBytes(range.materialSignature, material.emission, sizeof(material.emission));
          range.materialSignature = hashBytes(range.materialSignature, &material.shininess, sizeof(material.shininess));
        }
      }
      sharedIndexCount += range.relativeIndices.size();
      sharedRange = &sharedRanges.emplace(rangeKey, std::move(range)).first->second;
    }
    const bool replayRange = indexed && sharedRange;
    if (replayRange) lowered.materialSignature = sharedRange->materialSignature;
    else if (indexed) vertexRemap.assign(draw.geometry.vertexCount, UINT32_MAX);
    CoinBgfxVertex materialVertex{};
    const auto setMaterial = [&](CoinBgfxVertex & vertex,
                                 const CoinRenderMaterialSnapshot & material) {
      std::memcpy(vertex.color, material.diffuse, sizeof(vertex.color));
      std::memcpy(vertex.ambient, material.ambient, sizeof(vertex.ambient));
      std::memcpy(vertex.specular, material.specular, sizeof(vertex.specular));
      std::memcpy(vertex.emission, material.emission, sizeof(vertex.emission));
      vertex.material[0] = material.shininess;
      vertex.material[1] = state.lightModel == CoinRenderLightModel::PHONG ? 1.0f : 0.0f;
    };
    if (uniformMaterial) setMaterial(materialVertex, frame.materials[uniformMaterialSlot]);
    bool normalCached = false;
    float previousNormal[3], previousViewNormal[3];
    const uint32_t sourceCount = replayRange
      ? static_cast<uint32_t>(sharedRange->sourceVertices.size()) : draw.geometry.indexCount;
    for (uint32_t j = 0; j < sourceCount; ++j) {
      const uint32_t sourceIndex = replayRange ? sharedRange->sourceVertices[j]
        : frame.indices[draw.geometry.firstIndex + j];
      const CoinRenderVertexSnapshot & source = frame.vertices[sourceIndex];
      const CoinRenderMaterialSnapshot & material = frame.materials[source.materialSlot];
      if (!uniformMaterial && !replayRange) {
        lowered.materialSignature = hashBytes(lowered.materialSignature,
          material.diffuse, sizeof(material.diffuse));
        lowered.materialSignature = hashBytes(lowered.materialSignature,
          material.ambient, sizeof(material.ambient));
        lowered.materialSignature = hashBytes(lowered.materialSignature,
          material.specular, sizeof(material.specular));
        lowered.materialSignature = hashBytes(lowered.materialSignature,
          material.emission, sizeof(material.emission));
        lowered.materialSignature = hashBytes(lowered.materialSignature,
          &material.shininess, sizeof(material.shininess));
      }
      // Hash every material occurrence in the original index order even when
      // its vertex was already converted, preserving the grouping signature.
      if (indexed && !replayRange) {
        uint32_t & slot = vertexRemap[sourceIndex - draw.geometry.firstVertex];
        if (slot != UINT32_MAX) {
          candidate.indices.push_back(slot);
          continue;
        }
        slot = static_cast<uint32_t>(candidate.vertexCount());
      }
      const uint32_t emittedIndex = static_cast<uint32_t>(candidate.vertexCount());
      // Write into the output allocation, avoiding a complete temporary vertex
      // copy. Uniform material attributes and unused UVs are prepared once.
      CoinBgfxVertex temporaryVertex;
      if (compact) temporaryVertex = materialVertex;
      else candidate.vertices.push_back(materialVertex);
      CoinBgfxVertex & vertex = compact ? temporaryVertex : candidate.vertices.back();
      std::memcpy(vertex.position, source.position, sizeof(vertex.position));
      if (!uniformMaterial) setMaterial(vertex, material);
      vertex.material[2] = source.screenSpaceW;
      SbVec3f viewPosition;
      modelView.multVecMatrix(SbVec3f(source.position), viewPosition);
      if (source.fogEyeDepth >= 0) viewPosition[2] = -source.fogEyeDepth;
      viewPosition.getValue(vertex.viewPosition[0], vertex.viewPosition[1],
                            vertex.viewPosition[2]);
      if (batchable) std::memcpy(vertex.position, vertex.viewPosition, sizeof(vertex.position));
      if (!normalCached || std::memcmp(previousNormal, source.normal, sizeof(previousNormal)) != 0) {
        SbVec3f viewNormal;
        normalMatrix.multDirMatrix(SbVec3f(source.normal), viewNormal);
        if (viewNormal.normalize() == 0.0f) viewNormal.setValue(0.0f, 0.0f, 1.0f);
        viewNormal.getValue(previousViewNormal[0], previousViewNormal[1], previousViewNormal[2]);
        std::memcpy(previousNormal, source.normal, sizeof(previousNormal));
        normalCached = true;
      }
      std::memcpy(vertex.viewNormal, previousViewNormal, sizeof(vertex.viewNormal));
      if (state.hasTexture) {
        SbVec4f transformed;
        state.textureMatrix.multVecMatrix(
          SbVec4f(source.texcoord[0], source.texcoord[1], 0.0f, 1.0f),
          transformed);
        vertex.texcoord[0] = transformed[0];
        vertex.texcoord[1] = transformed[1];
      }
      for (size_t unit = 1; unit < COIN_RENDER_MAX_TEXTURE_UNITS; ++unit) {
        const auto & layer = state.extraTextures[unit - 1];
        if (!layer.enabled) continue;
        SbVec4f uv;
        layer.matrix.multVecMatrix(
          SbVec4f(source.extraTexcoords[unit - 1][0], source.extraTexcoords[unit - 1][1], 0, 1), uv);
        float * packed = &vertex.extraTexcoords[(unit - 1) / 2][((unit - 1) % 2) * 2];
        packed[0] = uv[0]; packed[1] = uv[1];
      }
      if (!replayRange) candidate.indices.push_back(emittedIndex);
      if (compact) {
        CoinBgfxVertexPrefix packed;
        std::memcpy(packed.data(), &vertex, sizeof(packed));
        candidate.packedVertices.push_back(packed);
      }
    }
    if (replayRange) for (uint32_t index : sharedRange->relativeIndices)
      candidate.indices.push_back(lowered.firstVertex + index);
    lowered.vertexCount = static_cast<uint32_t>(candidate.vertexCount()) - lowered.firstVertex;
    if (!frame.shadowGroups.empty()) candidate.shadowDraws.push_back(lowered);
    if (!draw.shadowLightSlot) {
      bool merged = false;
      if (batchable && previousBatchable && !candidate.draws.empty()) {
        auto & previous = candidate.draws.back();
        CoinBgfxDraw key = lowered;
        // Material values are already per-vertex attributes, not uniforms.
        key.materialSignature = previous.materialSignature;
        if (compareDrawGroupingKey(previous, key) == 0 &&
            std::memcmp(previous.mvp, lowered.mvp, sizeof(lowered.mvp)) == 0 &&
            previous.firstIndex + previous.indexCount == lowered.firstIndex &&
            previous.firstVertex + previous.vertexCount == lowered.firstVertex) {
          previous.indexCount += lowered.indexCount;
          previous.vertexCount += lowered.vertexCount;
          merged = true;
        }
      }
      if (!merged) candidate.draws.push_back(lowered);
      previousBatchable = batchable;
    }
  }

  candidate.textures.reserve(frame.textures.size());
  for (const CoinRenderTextureImageSnapshot & source : frame.textures) {
    CoinBgfxTexture texture;
    texture.width = source.width;
    texture.height = source.height;
    if (source.producerId) {
      diagnostic = "Unresolved scene texture producer at BGFX execution boundary";
      return false;
    }
    texture.gpuToken = source.gpuToken;
    texture.pixelsRgba = source.pixelsRgba;
    candidate.textures.push_back(std::move(texture));
  }
  for (int channel = 0; channel < 4; ++channel) {
    candidate.clearColor[channel] = frame.clearColor[channel];
  }
  output = std::move(candidate);
  return true;
}

void
CoinBgfxLowering::groupOpaqueDraws(
  const std::vector<CoinBgfxDraw> & draws,
  std::vector<CoinBgfxDraw> & output)
{
  for (const CoinBgfxDraw & draw : draws) {
    if (!opaqueDrawCanBeGrouped(draw)) {
      output = draws;
      return;
    }
  }
  std::vector<CoinBgfxDraw> candidate = draws;
  std::stable_sort(candidate.begin(), candidate.end(),
    [](const CoinBgfxDraw & lhs, const CoinBgfxDraw & rhs) {
      if (lhs.renderLayer != rhs.renderLayer)
        return lhs.renderLayer < rhs.renderLayer;
      if (lhs.renderLayer != 0) return false;
      if (lhs.blend != rhs.blend) return !lhs.blend;
      if (lhs.blend) return false;
      return compareDrawGroupingKey(lhs, rhs) < 0;
    });
  output.swap(candidate);
}

bool
CoinBgfxLowering::materialPatchRanges(
  const CoinBgfxPlan & base,
  const CoinBgfxPlan & updated,
  std::vector<CoinBgfxVertexRange> & ranges)
{
  std::vector<CoinBgfxVertexRange> candidate;
  if (base.usesCompactVertices || updated.usesCompactVertices || base.usesInstancing || updated.usesInstancing) return false;
  if (base.vertices.size() != updated.vertices.size() ||
      base.indices != updated.indices || base.draws.size() != updated.draws.size() ||
      base.textures.size() != updated.textures.size() ||
      std::memcmp(base.clearColor, updated.clearColor,
                  sizeof(base.clearColor)) != 0) return false;
  for (size_t i = 0; i < base.textures.size(); ++i) {
    if (!sameTexture(base.textures[i], updated.textures[i])) return false;
  }
  for (size_t i = 0; i < base.draws.size(); ++i) {
    if (!sameDrawExceptMaterial(base.draws[i], updated.draws[i]) ||
        base.draws[i].blend != updated.draws[i].blend) return false;
  }
  bool rangeOpen = false;
  for (size_t i = 0; i < base.vertices.size(); ++i) {
    if (!sameVertexExceptMaterial(base.vertices[i], updated.vertices[i])) return false;
    const bool changed = std::memcmp(&base.vertices[i], &updated.vertices[i],
                                     sizeof(CoinBgfxVertex)) != 0;
    if (changed && !rangeOpen) {
      CoinBgfxVertexRange range;
      range.first = static_cast<uint32_t>(i);
      range.count = 1;
      candidate.push_back(range);
      rangeOpen = true;
    } else if (changed) {
      ++candidate.back().count;
    } else {
      rangeOpen = false;
    }
  }
  ranges.swap(candidate);
  return true;
}

bool
CoinBgfxLowering::selectTransparencyStrategy(
  const std::vector<CoinBgfxDraw> & draws,
  CoinBgfxTransparencyMode configuredMode,
  bool weightedOitSupported,
  bool sortedLayersSupported,
  CoinBgfxTransparencyStrategy & selected,
  std::string & diagnostic)
{
  size_t transparentCount = 0;
  CoinBgfxTransparencyStrategy required = CoinBgfxTransparencyStrategy::OBJECT;
  for (const CoinBgfxDraw & draw : draws) {
    if (!draw.blend || !draw.deferred || draw.additive || draw.renderLayer != 0) continue;
    ++transparentCount;
    if (draw.transparencyStrategy == CoinBgfxTransparencyStrategy::SORTED_LAYERS) {
      required = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
    } else if (required == CoinBgfxTransparencyStrategy::OBJECT &&
               draw.transparencyStrategy == CoinBgfxTransparencyStrategy::WEIGHTED_OIT) {
      required = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
    }
  }

  const uint64_t requiredMechanism =
      required == CoinBgfxTransparencyStrategy::SORTED_LAYERS  ? COIN_RENDER_MECHANISM_PEELING
      : required == CoinBgfxTransparencyStrategy::WEIGHTED_OIT ? COIN_RENDER_MECHANISM_WEIGHTED_OIT
                                                               : COIN_RENDER_MECHANISM_OBJECT;
  const CoinRenderTransparencyMode mode =
      configuredMode == CoinBgfxTransparencyMode::AUTO     ? COIN_RENDER_TRANSPARENCY_COIN
      : configuredMode == CoinBgfxTransparencyMode::OBJECT ? COIN_RENDER_TRANSPARENCY_OBJECT
      : configuredMode == CoinBgfxTransparencyMode::WEIGHTED_OIT
          ? COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT
          : COIN_RENDER_TRANSPARENCY_PEELING;
  const auto requested =
      coin_render_requested_mechanism(mode, requiredMechanism, transparentCount != 0);
  selected = requested.mechanism == COIN_RENDER_MECHANISM_PEELING
                 ? CoinBgfxTransparencyStrategy::SORTED_LAYERS
             : requested.mechanism == COIN_RENDER_MECHANISM_WEIGHTED_OIT
                 ? CoinBgfxTransparencyStrategy::WEIGHTED_OIT
                 : CoinBgfxTransparencyStrategy::OBJECT;
  if (requested.reason != COIN_RENDER_SELECTION_SUPPORTED) {
    diagnostic = coin_render_selection_diagnostic(requested.reason);
    return false;
  }
  const uint64_t mechanism = requested.mechanism;
  const uint64_t implemented = COIN_RENDER_MECHANISM_OBJECT | COIN_RENDER_MECHANISM_PEELING |
                               COIN_RENDER_MECHANISM_WEIGHTED_OIT;
  const uint64_t available =
      COIN_RENDER_MECHANISM_OBJECT |
      (sortedLayersSupported ? uint64_t(COIN_RENDER_MECHANISM_PEELING) : 0) |
      (weightedOitSupported ? uint64_t(COIN_RENDER_MECHANISM_WEIGHTED_OIT) : 0);
  const auto selection = coin_render_selection(mechanism, implemented, available, 0);
  if (selection.reason != COIN_RENDER_SELECTION_SUPPORTED) {
    diagnostic = coin_render_selection_diagnostic(selection.reason);
    return false;
  }
  diagnostic.clear();
  return true;
}

bool
CoinBgfxLowering::patchCamera(const CoinRenderFramePlan & frame, int width, int height,
                            bool homogeneousDepth, const CoinBgfxPlan & base,
                            std::vector<CoinBgfxDraw> & output,
                            std::string & diagnostic)
{
  diagnostic.clear();
  if (base.usesInstancing) {
    const auto decline = [&](const char * reason) {
      diagnostic = std::string("BGFX instanced camera patch declined: ") + reason;
      return false;
    };
    if (!base.instancedCameraPatchable) return decline("unsafe anchor/model normal matrix");
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        frame.draws.empty() || frame.draws.size() != base.instances.size() || base.draws.empty()) return decline("size/source count");
    if (!frame.shadowGroups.empty() || !frame.textures.empty()) return decline("shadow/texture resources");
    for (int channel = 0; channel < 4; ++channel)
      if (!std::isfinite(frame.clearColor[channel]) || frame.clearColor[channel] != base.clearColor[channel])
        return decline("changed/non-finite clear color");
    const auto & firstSource = frame.draws.front();
    if (firstSource.renderStateSlot >= frame.renderStates.size()) return decline("render-state slot");
    const auto & firstState = frame.renderStates[firstSource.renderStateSlot];
    if (!instancedMatrixMagnitude(firstState.view)) return decline("view coefficient magnitude");
    SbMatrix delta, normalDelta;
    if (!CoinRenderTransformCore::cameraDelta(base.instanceCameraAnchorView, firstState.view,
                                             delta, normalDelta) ||
        !instancedMatrixMagnitude(delta) || !instancedMatrixMagnitude(normalDelta))
      return decline("non-rigid/unsafe camera delta");
    const bool cameraChanged = std::memcmp(firstState.view.getValue(),
      base.instanceCameraAnchorView.getValue(), sizeof(SbMat)) != 0;
    const auto deltaValues = delta.getValue();
    const auto normalValues = normalDelta.getValue();
    std::vector<CoinBgfxDraw> candidate = base.draws;
    size_t nextInstance = 0;
    // CAMERA_PATCH is a validated relationship: geometry, models, materials
    // and non-camera state remain identical to the cached source frame. Only
    // representative batch uniforms need refreshing, not every occurrence.
    for (auto & draw : candidate) {
      if (draw.firstInstance != nextInstance || draw.sourceDrawSlot != nextInstance ||
          !draw.instanceCount || draw.instanceCount > base.instances.size() - nextInstance)
        return decline("instance batch order/count");
      nextInstance += draw.instanceCount;
      const auto & source = frame.draws[draw.sourceDrawSlot];
      if (source.renderStateSlot >= frame.renderStates.size()) return decline("batch render-state slot");
      const auto & state = frame.renderStates[source.renderStateSlot];
      if (state.viewportSlot >= frame.viewports.size() || state.lightingSlot >= frame.lightingStates.size())
        return decline("viewport/lighting slot");
      const auto & viewport = frame.viewports[state.viewportSlot];
      if (viewport.x != draw.viewport[0] || viewport.y != draw.viewport[1] ||
          viewport.width != draw.viewport[2] || viewport.height != draw.viewport[3]) return decline("changed viewport");
      if (source.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST || source.renderLayer != 0 ||
          source.clearDepthBefore || source.shadowLightSlot || source.lineStripId ||
          source.geometry.indexCount != draw.indexCount || state.lightModel != CoinRenderLightModel::PHONG ||
          state.hasTexture || state.transparentMaterial || state.transparentTexture ||
          !state.clipPlanesWorld.empty() || state.fogMode != CoinRenderFogMode::NONE || state.shadowGroupSlot ||
          state.polygonOffsetEnabled || state.polygonLinePattern || state.linePattern != 0xffffu ||
          state.explicitDepthMask || state.cullMode != draw.cullMode || state.frontFace != draw.frontFace ||
          !state.depthTest || !state.depthWrite || state.depthFunction != draw.depthFunction ||
          state.depthRange[0] != draw.depthRange[0] || state.depthRange[1] != draw.depthRange[1])
        return decline("changed/unqualified non-camera state");
      for (const auto & texture : state.extraTextures) if (texture.enabled) return decline("extra texture unit");
      if (state.lightingSlot != firstState.lightingSlot ||
          std::memcmp(state.view.getValue(), firstState.view.getValue(), sizeof(SbMat)) != 0 ||
          std::memcmp(state.projectionCoin.getValue(), firstState.projectionCoin.getValue(), sizeof(SbMat)) != 0)
        return decline("different batch camera/lighting");
      const SbMatrix projection = CoinRenderTransformCore::projection(state.projectionCoin, homogeneousDepth);
      const SbMatrix mvp = projection * CoinRenderTransformCore::viewportTransform(viewport, width, height);
      if (!instancedMatrixMagnitude(mvp)) return decline("projection coefficient magnitude");
      std::memcpy(draw.mvp, mvp.getValue(), sizeof(draw.mvp));
      std::memset(draw.instanceCamera, 0, sizeof(draw.instanceCamera));
      if (cameraChanged) {
        for (int column = 0; column < 3; ++column) {
          for (int row = 0; row < 3; ++row) {
            draw.instanceCamera[column][row] = deltaValues[row][column];
            draw.instanceCamera[column + 3][row] = normalValues[row][column];
          }
          draw.instanceCamera[column][3] = deltaValues[3][column];
        }
        draw.instanceCamera[3][3] = 1.0f;
      }
      const auto & lighting = frame.lightingStates[state.lightingSlot];
      if (lighting.lights.size() > COIN_RENDER_MAX_LIGHTS) return decline("light count");
      copyLighting(lighting, draw);
    }
    if (nextInstance != base.instances.size()) return decline("incomplete instance batches");
    output.swap(candidate);
    return true;
  }
  if (frame.draws.size() != base.draws.size()) {
    diagnostic = "BGFX camera patch changed the draw count";
    return false;
  }
  for (const CoinBgfxDraw & draw : base.draws) {
    if (draw.blend) {
      diagnostic = "BGFX camera patch rebuilds transparent object order";
      return false;
    }
  }
  for (int channel = 0; channel < 4; ++channel) {
    if (!std::isfinite(frame.clearColor[channel])) {
      diagnostic = "BGFX camera patch has a non-finite clear color";
      return false;
    }
  }
  for (int channel = 0; channel < 4; ++channel) {
    if (frame.clearColor[channel] != base.clearColor[channel]) {
      diagnostic = "BGFX camera patch changed the clear color";
      return false;
    }
  }
  std::vector<CoinBgfxDraw> candidate = base.draws;
  for (size_t i = 0; i < frame.draws.size(); ++i) {
    const CoinRenderDrawPacket & draw = frame.draws[i];
    const CoinBgfxDraw & previous = base.draws[i];
    if (draw.renderStateSlot >= frame.renderStates.size()) {
      diagnostic = "BGFX camera patch has an invalid render-state slot";
      return false;
    }
    const CoinRenderRenderStateSnapshot & state = frame.renderStates[draw.renderStateSlot];
    if (state.viewportSlot >= frame.viewports.size()) {
      diagnostic = "BGFX camera patch has an invalid viewport slot";
      return false;
    }
    const CoinRenderViewportSnapshot & viewport = frame.viewports[state.viewportSlot];
    if (viewport.x != previous.viewport[0] || viewport.y != previous.viewport[1] ||
        viewport.width != previous.viewport[2] || viewport.height != previous.viewport[3]) {
      diagnostic = "BGFX camera patch changed the viewport";
      return false;
    }

    if (draw.topology != CoinRenderPrimitiveTopology::TRIANGLE_LIST ||
        draw.geometry.indexCount != previous.indexCount ||
        state.cullMode != previous.cullMode ||
        state.frontFace != previous.frontFace ||
        state.depthTest != previous.depthTest ||
        state.depthWrite != previous.depthWrite ||
        state.depthFunction != previous.depthFunction ||
        state.depthRange[0] != previous.depthRange[0] ||
        state.depthRange[1] != previous.depthRange[1] ||
        ((state.polygonOffsetEnabled &&
          (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle))
          ? state.polygonOffsetFactor : 0.0f) != previous.polygonOffsetFactor ||
        ((state.polygonOffsetEnabled &&
          (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle))
          ? state.polygonOffsetUnits : 0.0f) != previous.polygonOffsetUnits ||
        ((state.polygonOffsetEnabled &&
          (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle))
          ? state.polygonOffsetSlopeBias : 0.0f) != previous.polygonOffsetSlopeBias ||
        !state.clipPlanesWorld.empty() || previous.clipMeta[0] != 0 ||
        state.lightModel != CoinRenderLightModel::BASE_COLOR ||
        state.fogMode != CoinRenderFogMode::NONE || previous.fogColorMode[3] != 0.0f ||
        state.polygonOffsetPrimitiveStyle != 1 ||
        state.hasTexture != previous.hasTexture ||
        (state.hasTexture && state.textureImageSlot != previous.textureSlot)) {
      diagnostic = "BGFX camera patch changed non-camera draw state";
      return false;
    }
    if (!CoinRenderTransformCore::finiteMatrix(state.model) || !CoinRenderTransformCore::finiteMatrix(state.view) ||
        !CoinRenderTransformCore::finiteMatrix(state.projectionCoin)) {
      diagnostic = "BGFX camera patch has a non-finite matrix";
      return false;
    }

    const SbMatrix projection = CoinRenderTransformCore::projection(state.projectionCoin, homogeneousDepth);
    const SbMatrix viewportTransform = CoinRenderTransformCore::viewportTransform(viewport, width, height);
    const SbMatrix mvp = state.model * state.view * projection * viewportTransform;
    std::memcpy(candidate[i].mvp, mvp.getValue(), sizeof(candidate[i].mvp));
  }
  output.swap(candidate);
  return true;
}
