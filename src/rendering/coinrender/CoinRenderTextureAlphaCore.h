#ifndef COIN_RENDER_TEXTURE_ALPHA_CORE_H
#define COIN_RENDER_TEXTURE_ALPHA_CORE_H

#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <Inventor/nodes/SoSceneTexture2.h>

// Coin image flags decide scheduling/casting independently of sampled alpha.
// Shared by composition and shadow planning, including unresolved RTT tokens.
inline bool coin_render_texture_has_transparency(const CoinRenderTextureImageSnapshot & image)
{
  if (image.sceneTransparencyFunction == SoSceneTexture2::NONE) return false;
  if (image.sceneTransparencyFunction == SoSceneTexture2::ALPHA_BLEND) return true;
  if ((image.producerId || image.gpuToken) && !image.gpuOpaque) return true;
  return coin_render_image_has_transparency(image.pixelsRgba.data(),
                                           image.pixelsRgba.size() / 4, 4);
}

#endif
