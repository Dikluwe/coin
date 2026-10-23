#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderStateCore.h"
#include <climits>
#include <cstring>
#include <type_traits>

namespace {

uint64_t
stateHashMix(uint64_t hash, uint64_t value)
{
  hash ^= value;
  hash *= UINT64_C(1099511628211);
  return hash;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value, uint64_t>::type
stateHashValue(uint64_t hash, T value)
{
  return stateHashMix(hash, static_cast<uint64_t>(value));
}

template <typename T>
typename std::enable_if<std::is_enum<T>::value, uint64_t>::type
stateHashValue(uint64_t hash, T value)
{
  return stateHashMix(hash, static_cast<uint64_t>(value));
}

uint64_t
stateHashValue(uint64_t hash, float value)
{
  uint32_t bits = 0;
  // Coin vector and matrix equality treats both representations of zero as
  // equal. Preserve that contract in the hash instead of hashing raw bytes.
  if (value != 0.0f) std::memcpy(&bits, &value, sizeof(bits));
  return stateHashMix(hash, bits);
}

uint64_t
stateHashMatrix(uint64_t hash, const SbMatrix & matrix)
{
  const auto & values = matrix.getValue();
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      hash = stateHashValue(hash, values[row][column]);
  return hash;
}

uint64_t
stateHashVec3(uint64_t hash, const SbVec3f & value)
{
  for (int component = 0; component < 3; ++component)
    hash = stateHashValue(hash, value[component]);
  return hash;
}

uint64_t
stateHashVec4(uint64_t hash, const SbVec4f & value)
{
  for (int component = 0; component < 4; ++component)
    hash = stateHashValue(hash, value[component]);
  return hash;
}

uint64_t
stateHashTextureUnit(uint64_t hash, const CoinRenderTextureUnitSnapshot & unit)
{
  hash = stateHashValue(hash, unit.enabled);
  hash = stateHashValue(hash, unit.imageSlot);
  hash = stateHashValue(hash, unit.samplerSlot);
  hash = stateHashValue(hash, unit.model);
  for (float value : unit.blendColor) hash = stateHashValue(hash, value);
  return stateHashMatrix(hash, unit.matrix);
}

}

unsigned long
coin_render_state_hash(const CoinRenderRenderStateSnapshot & state)
{
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = stateHashValue(hash, state.portableShaderSlot);
  hash = stateHashValue(hash, state.clipPlanesWorld.size());
  for (const auto & plane : state.clipPlanesWorld) {
    hash = stateHashVec3(hash, plane.getNormal());
    hash = stateHashValue(hash, plane.getDistanceFromOrigin());
  }
  hash = stateHashMatrix(hash, state.model);
  hash = stateHashMatrix(hash, state.view);
  hash = stateHashMatrix(hash, state.projectionCoin);
  hash = stateHashValue(hash, state.materialSlot);
  hash = stateHashValue(hash, state.lightingSlot);
  hash = stateHashValue(hash, state.cameraSlot);
  hash = stateHashValue(hash, state.viewportSlot);
  hash = stateHashValue(hash, state.solidShape);
  hash = stateHashValue(hash, state.cullMode);
  hash = stateHashValue(hash, state.frontFace);
  hash = stateHashValue(hash, state.depthTest);
  hash = stateHashValue(hash, state.depthWrite);
  hash = stateHashValue(hash, state.colorWrite);
  hash = stateHashValue(hash, state.colorWriteMask);
  hash = stateHashValue(hash, state.textureProjection);
  hash = stateHashValue(hash, state.alphaTestFunction);
  hash = stateHashValue(hash, state.alphaTestReference);
  hash = stateHashValue(hash, state.explicitDepthMask);
  hash = stateHashValue(hash, state.depthFunction);
  for (float value : state.depthRange) hash = stateHashValue(hash, value);
  hash = stateHashValue(hash, state.polygonOffsetEnabled);
  hash = stateHashValue(hash, state.polygonOffsetFactor);
  hash = stateHashValue(hash, state.polygonOffsetUnits);
  hash = stateHashValue(hash, state.polygonOffsetMaxDepth);
  hash = stateHashValue(hash, state.polygonOffsetSlopeBias);
  hash = stateHashValue(hash, state.polygonOffsetStyles);
  hash = stateHashValue(hash, state.polygonOffsetPrimitiveStyle);
  hash = stateHashValue(hash, state.shadowGroupSlot);
  hash = stateHashValue(hash, state.shadowStyle);
  hash = stateHashValue(hash, state.shadowMapSize);
  hash = stateHashValue(hash, state.shadowMapCameraOverride);
  hash = stateHashValue(hash, state.flatShadowPrimary);
  hash = stateHashValue(hash, state.transparentMaterial);
  hash = stateHashValue(hash, state.transparentTexture);
  hash = stateHashValue(hash, state.rasterPixels);
  hash = stateHashValue(hash, state.rasterTransparent);
  hash = stateHashValue(hash, state.rasterForceBlend);
  hash = stateHashValue(hash, state.rasterShadowAnchorValid);
  hash = stateHashVec3(hash, state.rasterShadowAnchorWorld);
  hash = stateHashValue(hash, state.rasterShadowImagePhong);
  hash = stateHashValue(hash, state.rasterTextureSourceSlot);
  for (uint8_t value : state.rasterTextureProvenance) hash = stateHashValue(hash, value);
  for (const auto & value : state.rasterTextureCoordinates) hash = stateHashVec4(hash, value);
  hash = stateHashValue(hash, state.lightModel);
  hash = stateHashValue(hash, state.lineWidth);
  hash = stateHashValue(hash, state.pointSize);
  hash = stateHashValue(hash, state.linePattern);
  hash = stateHashValue(hash, state.linePatternScaleFactor);
  hash = stateHashValue(hash, state.polygonLinePattern);
  hash = stateHashValue(hash, state.preservePolygonEdgeDirection);
  hash = stateHashValue(hash, state.hasTexture);
  if (state.hasTexture) {
    hash = stateHashValue(hash, state.textureImageSlot);
    hash = stateHashValue(hash, state.samplerSlot);
    hash = stateHashValue(hash, state.textureModel);
    for (float value : state.textureBlendColor) hash = stateHashValue(hash, value);
    hash = stateHashMatrix(hash, state.textureMatrix);
  }
  hash = stateHashValue(hash, state.screenDoorTransparency);
  hash = stateHashValue(hash, state.transparencyType);
  hash = stateHashValue(hash, state.fogMode);
  for (float value : state.fogColor) hash = stateHashValue(hash, value);
  hash = stateHashValue(hash, state.fogStart);
  hash = stateHashValue(hash, state.fogEnd);
  for (const auto & unit : state.extraTextures) hash = stateHashTextureUnit(hash, unit);
  for (const auto & combine : state.textureCombines)
    for (const auto & instruction : combine.instructions)
      for (float value : instruction) hash = stateHashValue(hash, value);

#if ULONG_MAX == 0xffffffffUL
  hash ^= hash >> 32;
#endif
  return static_cast<unsigned long>(hash);
}

bool
coin_render_same_state(const CoinRenderRenderStateSnapshot &existing,
                       const CoinRenderRenderStateSnapshot &rs)
{
  return existing.portableShaderSlot == rs.portableShaderSlot &&
         existing.clipPlanesWorld == rs.clipPlanesWorld &&
         existing.materialSlot == rs.materialSlot &&
         existing.shadowGroupSlot == rs.shadowGroupSlot && existing.shadowStyle == rs.shadowStyle &&
         existing.shadowMapSize == rs.shadowMapSize &&
         existing.shadowMapCameraOverride == rs.shadowMapCameraOverride &&
         existing.flatShadowPrimary == rs.flatShadowPrimary &&
         existing.transparentMaterial == rs.transparentMaterial &&
         existing.transparentTexture == rs.transparentTexture &&
         existing.rasterPixels == rs.rasterPixels &&
         existing.rasterTransparent == rs.rasterTransparent &&
         existing.rasterForceBlend == rs.rasterForceBlend &&
         existing.rasterShadowAnchorValid == rs.rasterShadowAnchorValid &&
         existing.rasterShadowAnchorWorld == rs.rasterShadowAnchorWorld &&
         existing.rasterShadowImagePhong == rs.rasterShadowImagePhong &&
         existing.rasterTextureSourceSlot == rs.rasterTextureSourceSlot &&
         existing.rasterTextureCoordinates == rs.rasterTextureCoordinates &&
         existing.rasterTextureProvenance == rs.rasterTextureProvenance &&
         existing.lightingSlot == rs.lightingSlot && existing.lightModel == rs.lightModel &&
         existing.transparencyType == rs.transparencyType && existing.cameraSlot == rs.cameraSlot &&
         existing.viewportSlot == rs.viewportSlot && existing.solidShape == rs.solidShape &&
         existing.cullMode == rs.cullMode && existing.frontFace == rs.frontFace &&
         existing.depthTest == rs.depthTest && existing.depthWrite == rs.depthWrite &&
         existing.colorWrite == rs.colorWrite && existing.colorWriteMask == rs.colorWriteMask &&
         existing.textureProjection == rs.textureProjection &&
         existing.depthFunction == rs.depthFunction &&
         existing.alphaTestFunction == rs.alphaTestFunction &&
         existing.alphaTestReference == rs.alphaTestReference &&
         existing.explicitDepthMask == rs.explicitDepthMask &&
         existing.screenDoorTransparency == rs.screenDoorTransparency &&
         existing.depthRange[0] == rs.depthRange[0] && existing.depthRange[1] == rs.depthRange[1] &&
         existing.polygonOffsetEnabled == rs.polygonOffsetEnabled &&
         existing.polygonOffsetFactor == rs.polygonOffsetFactor &&
         existing.polygonOffsetUnits == rs.polygonOffsetUnits &&
         existing.polygonOffsetSlopeBias == rs.polygonOffsetSlopeBias &&
         existing.polygonOffsetMaxDepth == rs.polygonOffsetMaxDepth &&
         existing.polygonOffsetPrimitiveStyle == rs.polygonOffsetPrimitiveStyle &&
         existing.polygonOffsetStyles == rs.polygonOffsetStyles && existing.fogMode == rs.fogMode &&
         existing.fogStart == rs.fogStart && existing.fogEnd == rs.fogEnd &&
         std::memcmp(existing.fogColor, rs.fogColor, sizeof(rs.fogColor)) == 0 &&
         existing.lineWidth == rs.lineWidth && existing.pointSize == rs.pointSize &&
         coin_render_same_texture_units(existing.extraTextures, rs.extraTextures) &&
         std::memcmp(existing.textureCombines.data(), rs.textureCombines.data(),
                     sizeof(CoinRenderTextureCombineSnapshot) * COIN_RENDER_MAX_TEXTURE_UNITS) == 0 &&
         existing.hasTexture == rs.hasTexture && existing.linePattern == rs.linePattern &&
         existing.linePatternScaleFactor == rs.linePatternScaleFactor &&
         existing.polygonLinePattern == rs.polygonLinePattern &&
         existing.preservePolygonEdgeDirection == rs.preservePolygonEdgeDirection &&
         (!rs.hasTexture ||
          (existing.textureImageSlot == rs.textureImageSlot &&
           existing.samplerSlot == rs.samplerSlot && existing.textureModel == rs.textureModel &&
           std::memcmp(existing.textureBlendColor, rs.textureBlendColor,
                       sizeof(rs.textureBlendColor)) == 0 &&
           existing.textureMatrix == rs.textureMatrix)) &&
         existing.model == rs.model && existing.view == rs.view &&
         existing.projectionCoin == rs.projectionCoin;
}

bool
coin_render_same_state_except_camera(const CoinRenderRenderStateSnapshot &a,
                                     const CoinRenderRenderStateSnapshot &b)
{
  return a.portableShaderSlot == b.portableShaderSlot && a.clipPlanesWorld == b.clipPlanesWorld && a.model == b.model &&
         a.materialSlot == b.materialSlot && a.lightingSlot == b.lightingSlot &&
         a.shadowGroupSlot == b.shadowGroupSlot && a.shadowStyle == b.shadowStyle &&
         a.shadowMapSize == b.shadowMapSize &&
         a.shadowMapCameraOverride == b.shadowMapCameraOverride &&
         a.flatShadowPrimary == b.flatShadowPrimary &&
         a.transparentMaterial == b.transparentMaterial &&
         a.transparentTexture == b.transparentTexture && a.rasterPixels == b.rasterPixels &&
         a.rasterTransparent == b.rasterTransparent && a.rasterForceBlend == b.rasterForceBlend &&
         a.rasterShadowAnchorValid == b.rasterShadowAnchorValid &&
         a.rasterShadowAnchorWorld == b.rasterShadowAnchorWorld &&
         a.rasterShadowImagePhong == b.rasterShadowImagePhong &&
         a.rasterTextureSourceSlot == b.rasterTextureSourceSlot &&
         a.rasterTextureCoordinates == b.rasterTextureCoordinates &&
         a.rasterTextureProvenance == b.rasterTextureProvenance && a.cameraSlot == b.cameraSlot &&
         a.viewportSlot == b.viewportSlot && a.solidShape == b.solidShape && a.cullMode == b.cullMode &&
         a.frontFace == b.frontFace && a.depthTest == b.depthTest && a.depthWrite == b.depthWrite &&
         a.colorWrite == b.colorWrite && a.colorWriteMask == b.colorWriteMask &&
         a.textureProjection == b.textureProjection && a.explicitDepthMask == b.explicitDepthMask &&
         a.screenDoorTransparency == b.screenDoorTransparency &&
         a.depthFunction == b.depthFunction && a.alphaTestFunction == b.alphaTestFunction &&
         a.alphaTestReference == b.alphaTestReference &&
         std::memcmp(a.depthRange, b.depthRange, sizeof(a.depthRange)) == 0 &&
         a.polygonOffsetEnabled == b.polygonOffsetEnabled &&
         a.polygonOffsetFactor == b.polygonOffsetFactor &&
         a.polygonOffsetUnits == b.polygonOffsetUnits &&
         a.polygonOffsetSlopeBias == b.polygonOffsetSlopeBias &&
         a.polygonOffsetMaxDepth == b.polygonOffsetMaxDepth &&
         a.polygonOffsetStyles == b.polygonOffsetStyles &&
         a.polygonOffsetPrimitiveStyle == b.polygonOffsetPrimitiveStyle &&
         a.lightModel == b.lightModel && a.lineWidth == b.lineWidth && a.pointSize == b.pointSize &&
         a.linePattern == b.linePattern && a.linePatternScaleFactor == b.linePatternScaleFactor &&
         a.polygonLinePattern == b.polygonLinePattern &&
         a.preservePolygonEdgeDirection == b.preservePolygonEdgeDirection &&
         a.textureMatrix == b.textureMatrix &&
         coin_render_same_texture_units(a.extraTextures, b.extraTextures) &&
         std::memcmp(a.textureCombines.data(), b.textureCombines.data(),
                     sizeof(CoinRenderTextureCombineSnapshot) * COIN_RENDER_MAX_TEXTURE_UNITS) ==
             0 &&
         a.hasTexture == b.hasTexture && a.textureImageSlot == b.textureImageSlot &&
         a.samplerSlot == b.samplerSlot && a.textureModel == b.textureModel &&
         std::memcmp(a.textureBlendColor, b.textureBlendColor, sizeof(a.textureBlendColor)) == 0 &&
         a.transparencyType == b.transparencyType && a.fogMode == b.fogMode &&
         std::memcmp(a.fogColor, b.fogColor, sizeof(a.fogColor)) == 0 && a.fogStart == b.fogStart &&
         a.fogEnd == b.fogEnd;
}
