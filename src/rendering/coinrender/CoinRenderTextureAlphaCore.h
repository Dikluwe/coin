#ifndef COIN_RENDER_TEXTURE_ALPHA_CORE_H
#define COIN_RENDER_TEXTURE_ALPHA_CORE_H

#include "rendering/coinrender/CoinRenderTextureFormatCore.h"
#include <Inventor/nodes/SoSceneTexture2.h>

// Coin image flags decide scheduling/casting independently of sampled alpha.
// Shared by composition and shadow planning, including unresolved RTT tokens.
inline bool coin_render_scene_texture_policy_supported(int32_t policy)
{
  return policy == SoSceneTexture2::NONE || policy == SoSceneTexture2::ALPHA_BLEND ||
         policy == SoSceneTexture2::ALPHA_TEST;
}
inline bool coin_render_scene_texture_forces_transparency(int32_t policy)
{
  // Coin/GL sets FORCE_TRANSPARENCY_TRUE for both. useAlphaTest() is not
  // consumed by the renderer: do not infer an automatic discard threshold.
  return policy == SoSceneTexture2::ALPHA_BLEND || policy == SoSceneTexture2::ALPHA_TEST;
}
inline bool coin_render_texture_has_transparency(const CoinRenderTextureImageSnapshot & image)
{
  if (image.sceneTransparencyFunction == SoSceneTexture2::NONE) return false;
  if (coin_render_scene_texture_forces_transparency(image.sceneTransparencyFunction)) return true;
  if ((image.producerId || image.gpuToken) && !image.gpuOpaque) return true;
  if(image.producerId || image.gpuToken)return false;
  if(image.format==CoinRenderTextureFormat::RGBA8_LINEAR || image.format==CoinRenderTextureFormat::RGBA8_SRGB)
    return coin_render_image_has_transparency(image.pixelsRgba.data(),image.pixelsRgba.size()/4,4);
  const size_t bytes=CoinRenderTextureFormatCore::levelBytes(image.width,image.height,image.format);
  if(!bytes || image.pixelsRgba.size()!=bytes) return true;
  for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)
    if(CoinRenderTextureFormatCore::texel(image.pixelsRgba,0,image.width,image.format,x,y)[3]<1.f)return true;
  return false;
}

#endif
