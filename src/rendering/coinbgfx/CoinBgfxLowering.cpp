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
#include <utility>
#include <unordered_map>

namespace {
uint64_t hashBytes(uint64_t hash, const void * data, size_t bytes)
{
  const unsigned char * value = static_cast<const unsigned char *>(data);
  for (size_t i = 0; i < bytes; ++i) {
    hash ^= value[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
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
    uint64_t(plan.textures.capacity()) * sizeof(CoinBgfxTexture);
  for (const auto & texture : plan.textures) metadataBytes += texture.pixelsRgba.capacity();
  if (metadataBytes > metadataBudget) return false;
  // Large uploads can transfer the vertex allocation to BGFX before caching.
  // Keep their count, and also make retaining an already released plan safe.
  plan.uploadedVertexCount = plan.vertexCount();
  if (!plan.indices.empty()) plan.uploadedIndexCount = plan.indices.size();
  // Account for transferred vertices as well, so their remaining indices do
  // not accidentally become eligible for the small-plan material cache.
  const uint64_t geometryBytes = (plan.usesCompactVertices
    ? uint64_t(std::max(plan.packedVertices.capacity(), plan.uploadedVertexCount)) * sizeof(CoinBgfxVertexPrefix)
    : uint64_t(std::max(plan.vertices.capacity(), plan.uploadedVertexCount)) * sizeof(CoinBgfxVertex)) +
    uint64_t(std::max(plan.indices.capacity(), plan.uploadedIndexCount)) * sizeof(uint32_t);
  if (plan.usesCompactVertices || geometryBytes + metadataBytes > geometryBudget) {
    std::vector<CoinBgfxVertex>().swap(plan.vertices);
    std::vector<CoinBgfxVertexPrefix>().swap(plan.packedVertices);
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
    const uint32_t uniformMaterialSlot = frame.vertices[frame.indices[draw.geometry.firstIndex]].materialSlot;
    bool uniformMaterial = true;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
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
    if (indexed) vertexRemap.assign(draw.geometry.vertexCount, UINT32_MAX);
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
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t sourceIndex = frame.indices[draw.geometry.firstIndex + j];
      const CoinRenderVertexSnapshot & source = frame.vertices[sourceIndex];
      const CoinRenderMaterialSnapshot & material = frame.materials[source.materialSlot];
      if (!uniformMaterial) {
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
      if (indexed) {
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
      candidate.indices.push_back(emittedIndex);
      if (compact) {
        CoinBgfxVertexPrefix packed;
        std::memcpy(packed.data(), &vertex, sizeof(packed));
        candidate.packedVertices.push_back(packed);
      }
    }
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
  if (base.usesCompactVertices || updated.usesCompactVertices) return false;
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
