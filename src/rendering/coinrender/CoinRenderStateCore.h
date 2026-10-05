#ifndef COIN_RENDER_STATE_CORE_H
#define COIN_RENDER_STATE_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <cstring>

template <size_t N>
inline bool coin_render_same_texture_units(const CoinRenderTextureUnitSnapshot (&a)[N],
                                           const CoinRenderTextureUnitSnapshot (&b)[N])
{
  // Compare values, never the padding following the enabled flag.
  for (size_t i = 0; i < N; ++i) {
    if (a[i].enabled != b[i].enabled || a[i].imageSlot != b[i].imageSlot ||
        a[i].samplerSlot != b[i].samplerSlot || a[i].model != b[i].model ||
        std::memcmp(a[i].blendColor, b[i].blendColor, sizeof(a[i].blendColor)) != 0 ||
        a[i].matrix != b[i].matrix) return false;
  }
  return true;
}

inline bool
coin_render_same_state_except_camera(const CoinRenderRenderStateSnapshot & a,
                            const CoinRenderRenderStateSnapshot & b)
{
  return a.clipPlanesWorld == b.clipPlanesWorld && a.model == b.model &&
    a.materialSlot == b.materialSlot && a.lightingSlot == b.lightingSlot &&
    a.shadowGroupSlot == b.shadowGroupSlot && a.shadowStyle == b.shadowStyle &&
    a.transparentMaterial == b.transparentMaterial &&
    a.transparentTexture == b.transparentTexture &&
    a.rasterPixels == b.rasterPixels &&
    a.rasterTransparent == b.rasterTransparent &&
    a.rasterForceBlend == b.rasterForceBlend &&
    a.cameraSlot == b.cameraSlot && a.viewportSlot == b.viewportSlot &&
    a.cullMode == b.cullMode && a.frontFace == b.frontFace &&
    a.depthTest == b.depthTest && a.depthWrite == b.depthWrite &&
    a.explicitDepthMask == b.explicitDepthMask &&
    a.screenDoorTransparency == b.screenDoorTransparency &&
    a.depthFunction == b.depthFunction &&
    a.alphaTestFunction == b.alphaTestFunction && a.alphaTestReference == b.alphaTestReference &&
    std::memcmp(a.depthRange, b.depthRange, sizeof(a.depthRange)) == 0 &&
    a.polygonOffsetEnabled == b.polygonOffsetEnabled &&
    a.polygonOffsetFactor == b.polygonOffsetFactor &&
    a.polygonOffsetUnits == b.polygonOffsetUnits &&
    a.polygonOffsetSlopeBias == b.polygonOffsetSlopeBias &&
    a.polygonOffsetMaxDepth == b.polygonOffsetMaxDepth &&
    a.polygonOffsetStyles == b.polygonOffsetStyles &&
    a.polygonOffsetPrimitiveStyle == b.polygonOffsetPrimitiveStyle &&
    a.lightModel == b.lightModel && a.lineWidth == b.lineWidth &&
    a.pointSize == b.pointSize && a.linePattern == b.linePattern &&
    a.linePatternScaleFactor == b.linePatternScaleFactor &&
    a.polygonLinePattern == b.polygonLinePattern &&
    a.textureMatrix == b.textureMatrix &&
    coin_render_same_texture_units(a.extraTextures, b.extraTextures) &&
    std::memcmp(a.textureCombines, b.textureCombines, sizeof(a.textureCombines)) == 0 &&
    a.hasTexture == b.hasTexture && a.textureImageSlot == b.textureImageSlot &&
    a.samplerSlot == b.samplerSlot && a.textureModel == b.textureModel &&
    std::memcmp(a.textureBlendColor, b.textureBlendColor,
                sizeof(a.textureBlendColor)) == 0 &&
    a.transparencyType == b.transparencyType && a.fogMode == b.fogMode &&
    std::memcmp(a.fogColor, b.fogColor, sizeof(a.fogColor)) == 0 &&
    a.fogStart == b.fogStart && a.fogEnd == b.fogEnd;
}
#endif
