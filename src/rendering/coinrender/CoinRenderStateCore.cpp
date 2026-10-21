#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderStateCore.h"
#include <cstring>

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
         a.viewportSlot == b.viewportSlot && a.cullMode == b.cullMode &&
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
