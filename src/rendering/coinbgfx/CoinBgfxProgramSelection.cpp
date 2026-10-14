#include "rendering/coinbgfx/CoinBgfxProgramSelection.h"

bool
coin_bgfx_solid_program(const std::vector<CoinBgfxDraw> &draws, bool hasShadows,
                        CoinBgfxTransparencyStrategy strategy)
{
  if (hasShadows || strategy != CoinBgfxTransparencyStrategy::OBJECT || draws.empty())
    return false;
  for (const CoinBgfxDraw &draw : draws) {
    if (draw.blend || draw.alpha != 1.0f || draw.hasTexture || draw.fogColorMode[3] != 0.0f ||
        draw.clipMeta[0] != 0.0f || draw.screenDoor[0] != 0.0f || draw.screenDoor[3] != 0.0f)
      return false;
    for (const auto &layer : draw.extraTextures)
      if (layer.enabled)
        return false;
  }
  return true;
}
