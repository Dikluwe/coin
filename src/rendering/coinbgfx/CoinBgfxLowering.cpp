#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinbgfx/CoinBgfxLowering.h"
#include "rendering/coinrender/CoinRenderComposition.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <utility>

namespace {
bool finiteMatrix(const SbMatrix & matrix)
{
  const float (*value)[4] = matrix.getValue();
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      if (!std::isfinite(value[row][col])) return false;
    }
  }
  return true;
}

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
  if (result == 0) result = compareBytes(lhs.viewport, rhs.viewport, sizeof(lhs.viewport));
  if (result == 0) result = compareValue(lhs.materialSignature, rhs.materialSignature);
  if (result == 0) result = compareBytes(lhs.fogColorMode, rhs.fogColorMode, sizeof(lhs.fogColorMode));
  if (result == 0) result = compareBytes(lhs.fogRange, rhs.fogRange, sizeof(lhs.fogRange));
  if (result == 0) result = compareBytes(lhs.extraTextures, rhs.extraTextures, sizeof(lhs.extraTextures));
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
     draw.polygonOffsetFactor == 0.0f && draw.polygonOffsetUnits == 0.0f);
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
CoinBgfxLowering::clipViewport(const int32_t viewport[4], int width, int height,
                            int32_t clipped[4])
{
  const int64_t left = std::max<int64_t>(0, viewport[0]);
  const int64_t bottom = std::max<int64_t>(0, viewport[1]);
  const int64_t right = std::min<int64_t>(width, int64_t(viewport[0]) + viewport[2]);
  const int64_t top = std::min<int64_t>(height, int64_t(viewport[1]) + viewport[3]);
  if (right <= left || top <= bottom) return false;
  clipped[0] = static_cast<int32_t>(left);
  clipped[1] = static_cast<int32_t>(bottom);
  clipped[2] = static_cast<int32_t>(right - left);
  clipped[3] = static_cast<int32_t>(top - bottom);
  return true;
}

bool
CoinBgfxLowering::lower(const CoinRenderFramePlan & frame, int width, int height,
                      bool homogeneousDepth, CoinBgfxPlan & output,
                      std::string & diagnostic)
{
  diagnostic.clear();
  CoinBgfxPlan candidate;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    diagnostic = "BGFX evaluation requires a nonzero target up to 16384 pixels per side";
    return false;
  }
  if (!frame.isValid(&diagnostic)) return false;
  std::vector<CoinRenderCompositionItem> order;
  if (!coin_render_composition_order(frame, order, diagnostic, true)) return false;

  for (const CoinRenderCompositionItem & item : order) {
    const CoinRenderDrawPacket & draw = frame.draws[item.drawIndex];
    const CoinRenderRenderStateSnapshot & state = frame.renderStates[draw.renderStateSlot];
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
    float maxAlpha = 0.0f;
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t index = frame.indices[draw.geometry.firstIndex + j];
      if (index < draw.geometry.firstVertex ||
          index >= draw.geometry.firstVertex + draw.geometry.vertexCount) {
        diagnostic = "BGFX draw index escapes its vertex range";
        return false;
      }
      const CoinRenderMaterialSnapshot & material = frame.materials[frame.vertices[index].materialSlot];
      if (!std::isfinite(material.diffuse[0]) || !std::isfinite(material.diffuse[1]) ||
          !std::isfinite(material.diffuse[2]) ||
          !std::isfinite(material.diffuse[3])) {
        diagnostic = "BGFX evaluation received non-finite material color";
        return false;
      }
      if (material.diffuse[3] > maxAlpha) maxAlpha = material.diffuse[3];
    }
    const SbMatrix clipConversion(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);
    const SbMatrix modelView = state.model * state.view;
    const float normalDeterminant = modelView.det4();
    const SbMatrix normalMatrix = std::abs(normalDeterminant) > 1.0e-12f
      ? modelView.inverse().transpose() : SbMatrix::identity();
    const SbMatrix projection = homogeneousDepth
      ? state.projectionCoin : state.projectionCoin * clipConversion;
    const float sx = static_cast<float>(viewport.width) / static_cast<float>(width);
    const float sy = static_cast<float>(viewport.height) / static_cast<float>(height);
    const float tx = (2.0f * viewport.x + viewport.width) / static_cast<float>(width) - 1.0f;
    const float ty = (2.0f * viewport.y + viewport.height) / static_cast<float>(height) - 1.0f;
    const SbMatrix viewportTransform(
      sx, 0.0f, 0.0f, 0.0f,
      0.0f, sy, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      tx, ty, 0.0f, 1.0f);
    const SbMatrix mvp = modelView * projection * viewportTransform;
    CoinBgfxDraw lowered{};
    std::memcpy(lowered.mvp, mvp.getValue(), sizeof(lowered.mvp));
    lowered.firstVertex = static_cast<uint32_t>(candidate.vertices.size());
    lowered.vertexCount = draw.geometry.indexCount;
    lowered.firstIndex = static_cast<uint32_t>(candidate.indices.size());
    lowered.indexCount = draw.geometry.indexCount;
    lowered.cullMode = state.cullMode;
    lowered.frontFace = state.frontFace;
    lowered.depthTest = state.depthTest;
    lowered.depthWrite = state.depthWrite;
    lowered.depthFunction = state.depthFunction;
    lowered.depthRange[0] = state.depthRange[0];
    lowered.depthRange[1] = state.depthRange[1];
    if (state.polygonOffsetEnabled &&
        (state.polygonOffsetStyles & state.polygonOffsetPrimitiveStyle) != 0) {
      lowered.polygonOffsetFactor = state.polygonOffsetFactor;
      lowered.polygonOffsetUnits = state.polygonOffsetUnits;
    }
    lowered.blend = item.blend;
    lowered.sourceNodeId = draw.sourceNodeId;
    lowered.sortTriangles = item.sortTriangles && state.polygonOffsetPrimitiveStyle == 1;
    lowered.deferred = item.deferred && draw.renderLayer == 0;
    lowered.additive = item.additive;
    if (lowered.deferred) {
      if (!(state.explicitDepthMask & 1)) lowered.depthTest = true;
      if (!(state.explicitDepthMask & 2)) lowered.depthWrite = false;
      if (!(state.explicitDepthMask & 4)) lowered.depthFunction = CoinRenderDepthFunction::LEQUAL;
      if (!(state.explicitDepthMask & 8)) { lowered.depthRange[0] = 0; lowered.depthRange[1] = 1; }
    }
    if (state.transparencyType == SoGLRenderAction::SCREEN_DOOR &&
        state.polygonOffsetPrimitiveStyle == 1)
      lowered.screenDoor[0] = std::min(64, std::max(0, int((state.screenDoorTransparency >= 0 ? state.screenDoorTransparency : frame.materials[state.materialSlot].transparency) * 64.0f)));
    lowered.screenDoor[3] = state.transparencyType == SoGLRenderAction::SCREEN_DOOR ? 1.0f : 0.0f;

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
    for (uint32_t j = 0; j < draw.geometry.indexCount; ++j) {
      const uint32_t sourceIndex = frame.indices[draw.geometry.firstIndex + j];
      const CoinRenderVertexSnapshot & source = frame.vertices[sourceIndex];
      CoinBgfxVertex vertex{};
      std::memcpy(vertex.position, source.position, sizeof(vertex.position));
      const CoinRenderMaterialSnapshot & material = frame.materials[source.materialSlot];
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
      std::memcpy(vertex.color, material.diffuse, sizeof(vertex.color));
      std::memcpy(vertex.ambient, material.ambient, sizeof(vertex.ambient));
      std::memcpy(vertex.specular, material.specular, sizeof(vertex.specular));
      std::memcpy(vertex.emission, material.emission, sizeof(vertex.emission));
      vertex.material[2] = source.screenSpaceW;
      vertex.material[0] = material.shininess;
      vertex.material[1] = state.lightModel == CoinRenderLightModel::PHONG ? 1.0f : 0.0f;
      SbVec3f viewPosition;
      modelView.multVecMatrix(SbVec3f(source.position), viewPosition);
      if (source.fogEyeDepth >= 0) viewPosition[2] = -source.fogEyeDepth;
      viewPosition.getValue(vertex.viewPosition[0], vertex.viewPosition[1],
                            vertex.viewPosition[2]);
      SbVec3f viewNormal;
      normalMatrix.multDirMatrix(SbVec3f(source.normal), viewNormal);
      if (viewNormal.normalize() == 0.0f) viewNormal.setValue(0.0f, 0.0f, 1.0f);
      viewNormal.getValue(vertex.viewNormal[0], vertex.viewNormal[1],
                          vertex.viewNormal[2]);
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
      candidate.indices.push_back(
        static_cast<uint32_t>(candidate.vertices.size()));
      candidate.vertices.push_back(vertex);
    }
    if (item.blend && item.sortTriangles && state.polygonOffsetPrimitiveStyle == 1) {
      const uint32_t first = lowered.firstIndex;
      std::vector<uint32_t> triangles(lowered.indexCount / 3);
      for (uint32_t t = 0; t < triangles.size(); ++t) triangles[t] = t;
      const auto depth = [&](uint32_t t) {
        float z = 0;
        for (uint32_t v = 0; v < 3; ++v)
          z += candidate.vertices[candidate.indices[first + t * 3 + v]].viewPosition[2];
        return z;
      };
      std::stable_sort(triangles.begin(), triangles.end(),
        [&](uint32_t a, uint32_t b) { return depth(a) < depth(b); });
      std::vector<uint32_t> sorted;
      sorted.reserve(lowered.indexCount);
      for (auto t : triangles) for (uint32_t v = 0; v < 3; ++v)
        sorted.push_back(candidate.indices[first + t * 3 + v]);
      std::copy(sorted.begin(), sorted.end(), candidate.indices.begin() + first);
    }
    candidate.draws.push_back(lowered);
  }

  // Material binding may split a single Coin shape into multiple packets.
  // Sort all of that shape's triangles, not just each material run separately.
  std::vector<CoinBgfxDraw> sortedDraws;
  for (size_t begin = 0; begin < candidate.draws.size();) {
    size_t end = begin + 1;
    const auto & first = candidate.draws[begin];
    if (first.sortTriangles && first.blend && first.sourceNodeId && first.renderLayer == 0) {
      while (end < candidate.draws.size()) {
        const auto & next = candidate.draws[end];
        if (!next.sortTriangles || !next.blend || next.sourceNodeId != first.sourceNodeId ||
            next.renderLayer != first.renderLayer || std::memcmp(next.mvp, first.mvp, sizeof(first.mvp)) != 0) break;
        ++end;
      }
    }
    if (end == begin + 1) sortedDraws.push_back(first);
    else {
      std::vector<CoinBgfxDraw> triangles;
      for (size_t drawIndex = begin; drawIndex < end; ++drawIndex) {
        const auto & source = candidate.draws[drawIndex];
        for (uint32_t index = source.firstIndex; index < source.firstIndex + source.indexCount; index += 3) {
          auto triangle = source;
          triangle.firstIndex = index; triangle.indexCount = 3;
          triangle.firstVertex = candidate.indices[index]; triangle.vertexCount = 3;
          triangles.push_back(triangle);
        }
      }
      const auto eyeZ = [&](const CoinBgfxDraw & draw) {
        float z = 0;
        for (uint32_t i = 0; i < 3; ++i)
          z += candidate.vertices[candidate.indices[draw.firstIndex + i]].viewPosition[2];
        return z;
      };
      std::stable_sort(triangles.begin(), triangles.end(), [&](const CoinBgfxDraw & a, const CoinBgfxDraw & b) {
        return eyeZ(a) < eyeZ(b);
      });
      sortedDraws.insert(sortedDraws.end(), triangles.begin(), triangles.end());
    }
    begin = end;
  }
  candidate.draws.swap(sortedDraws);
  candidate.textures.reserve(frame.textures.size());
  for (const CoinRenderTextureImageSnapshot & source : frame.textures) {
    CoinBgfxTexture texture;
    texture.width = source.width;
    texture.height = source.height;
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

  if (configuredMode == CoinBgfxTransparencyMode::OBJECT)
    selected = CoinBgfxTransparencyStrategy::OBJECT;
  else if (configuredMode == CoinBgfxTransparencyMode::WEIGHTED_OIT)
    selected = CoinBgfxTransparencyStrategy::WEIGHTED_OIT;
  else if (configuredMode == CoinBgfxTransparencyMode::SORTED_LAYERS)
    selected = CoinBgfxTransparencyStrategy::SORTED_LAYERS;
  else {
    selected = required;
    // AUTO preserves the selected Coin semantics; OIT is an explicit extension.
  }

  if (transparentCount == 0) selected = CoinBgfxTransparencyStrategy::OBJECT;
  if (selected == CoinBgfxTransparencyStrategy::WEIGHTED_OIT && !weightedOitSupported) {
    diagnostic = "weighted_oit was selected but MRT with independent blending and RGBA16F/R16F targets is unavailable; no fallback was applied";
    return false;
  }
  if (selected == CoinBgfxTransparencyStrategy::SORTED_LAYERS && !sortedLayersSupported) {
    diagnostic = "sorted_layers was selected but sampleable D32F/RGBA8 targets are unavailable; no fallback was applied";
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
  const SbMatrix clipConversion(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.5f, 1.0f);
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
        state.lightModel != CoinRenderLightModel::BASE_COLOR ||
        state.fogMode != CoinRenderFogMode::NONE || previous.fogColorMode[3] != 0.0f ||
        state.polygonOffsetPrimitiveStyle != 1 ||
        state.hasTexture != previous.hasTexture ||
        (state.hasTexture && state.textureImageSlot != previous.textureSlot)) {
      diagnostic = "BGFX camera patch changed non-camera draw state";
      return false;
    }
    if (!finiteMatrix(state.model) || !finiteMatrix(state.view) ||
        !finiteMatrix(state.projectionCoin)) {
      diagnostic = "BGFX camera patch has a non-finite matrix";
      return false;
    }

    const SbMatrix projection = homogeneousDepth
      ? state.projectionCoin : state.projectionCoin * clipConversion;
    const float sx = static_cast<float>(viewport.width) / static_cast<float>(width);
    const float sy = static_cast<float>(viewport.height) / static_cast<float>(height);
    const float tx = (2.0f * viewport.x + viewport.width) / static_cast<float>(width) - 1.0f;
    const float ty = (2.0f * viewport.y + viewport.height) / static_cast<float>(height) - 1.0f;
    const SbMatrix viewportTransform(
      sx, 0.0f, 0.0f, 0.0f,
      0.0f, sy, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      tx, ty, 0.0f, 1.0f);
    const SbMatrix mvp = state.model * state.view * projection * viewportTransform;
    std::memcpy(candidate[i].mvp, mvp.getValue(), sizeof(candidate[i].mvp));
  }
  output.swap(candidate);
  return true;
}
