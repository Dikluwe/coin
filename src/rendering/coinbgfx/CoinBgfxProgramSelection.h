#ifndef COIN_BGFX_PROGRAM_SELECTION_H
#define COIN_BGFX_PROGRAM_SELECTION_H

#include "rendering/coinbgfx/CoinBgfxLowering.h"

// The solid fragment stage preserves the base vertex stage's material/lighting
// and the shared window-depth contract. Qualify every draw before omitting any
// fragment surface operation; mixed or advanced plans keep the general stage.
inline bool
coin_bgfx_solid_program(const std::vector<CoinBgfxDraw> & draws,
                        bool hasShadows,
                        CoinBgfxTransparencyStrategy strategy)
{
  if (hasShadows || strategy != CoinBgfxTransparencyStrategy::OBJECT || draws.empty())
    return false;
  for (const CoinBgfxDraw & draw : draws) {
    if (draw.blend || draw.alpha != 1.0f || draw.hasTexture ||
        draw.fogColorMode[3] != 0.0f || draw.clipMeta[0] != 0.0f ||
        draw.screenDoor[0] != 0.0f || draw.screenDoor[3] != 0.0f)
      return false;
    for (const auto & layer : draw.extraTextures)
      if (layer.enabled) return false;
  }
  return true;
}

#endif
