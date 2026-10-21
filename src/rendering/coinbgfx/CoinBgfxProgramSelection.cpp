#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"
#include <cmath>

bool
coin_bgfx_solid_program(const std::vector<CoinBgfxDraw> &draws, bool hasShadows,
                        CoinBgfxTransparencyStrategy strategy)
{
  if (hasShadows || strategy != CoinBgfxTransparencyStrategy::OBJECT || draws.empty())
    return false;
  for (const CoinBgfxDraw &draw : draws) {
    // Blending and vertex alpha do not require the general surface stage:
    // both stages feed the same fixed blend/depth state after AlphaTest.
    // SCREEN_DOOR at level zero still forces alpha before that comparison.
    if (draw.portableShaderSlot || draw.transparencyStrategy != CoinBgfxTransparencyStrategy::OBJECT ||
        !std::isfinite(draw.alpha) || draw.alpha < 0.0f || draw.alpha > 1.0f ||
        static_cast<uint32_t>(draw.alphaTestFunction) >
          static_cast<uint32_t>(CoinRenderAlphaTestFunction::NOTEQUAL) ||
        !std::isfinite(draw.alphaTestReference) || draw.alphaTestReference < 0.0f ||
        draw.alphaTestReference > 1.0f || draw.hasTexture || draw.fogColorMode[3] != 0.0f ||
        draw.clipMeta[0] != 0.0f || draw.screenDoor[0] != 0.0f)
      return false;
    for (int component = 0; component < 4; ++component) {
      if (!std::isfinite(draw.screenDoor[component]) ||
          !std::isfinite(draw.fogColorMode[component]) ||
          !std::isfinite(draw.fogRange[component]) ||
          !std::isfinite(draw.clipMeta[component]))
        return false;
    }
    if ((draw.screenDoor[3] != 0.0f && draw.screenDoor[3] != 1.0f) ||
        draw.screenDoor[1] < 0.0f ||
        (draw.screenDoor[2] != 0.0f && draw.screenDoor[2] != 1.0f) ||
        (draw.screenDoor[3] == 1.0f && draw.blend))
      return false;
    for (const auto &layer : draw.extraTextures)
      if (layer.enabled)
        return false;
  }
  return true;
}

bool
coin_bgfx_texture2d_program(const std::vector<CoinBgfxDraw> &draws, bool hasShadows,
                            CoinBgfxTransparencyStrategy strategy)
{
  if (hasShadows || strategy != CoinBgfxTransparencyStrategy::OBJECT || draws.empty())
    return false;
  bool textured = false;
  for (const CoinBgfxDraw &draw : draws) {
    if (draw.portableShaderSlot || !std::isfinite(draw.alpha) || draw.alpha < 0.0f || draw.alpha > 1.0f ||
        static_cast<uint32_t>(draw.alphaTestFunction) >
          static_cast<uint32_t>(CoinRenderAlphaTestFunction::NOTEQUAL) ||
        !std::isfinite(draw.alphaTestReference) || draw.alphaTestReference < 0.0f ||
        draw.alphaTestReference > 1.0f)
      return false;
    for (int component = 0; component < 4; ++component) {
      if (!std::isfinite(draw.fogColorMode[component]) ||
          !std::isfinite(draw.fogRange[component]) ||
          !std::isfinite(draw.clipMeta[component]))
        return false;
    }
    if (draw.portableShaderSlot || draw.transparencyStrategy != CoinBgfxTransparencyStrategy::OBJECT ||
        draw.instanceCount != 0 || draw.cubeFaceSize || draw.cubeSourceComponents ||
        draw.cubeMipmapped || draw.volumeWidth || draw.volumeHeight || draw.volumeDepth ||
        draw.volumeMipmapped)
      return false;
    for (const auto &layer : draw.extraTextures)
      if (layer.enabled)
        return false;
    // Shared sampling, combine, fog, clipping, AlphaTest and stipple remain
    // unchanged. Do not narrow admission based on any of those active states.
    // Their ordinary validation still precedes backend program selection.
    textured = textured || draw.hasTexture;
  }
  return textured;
}
