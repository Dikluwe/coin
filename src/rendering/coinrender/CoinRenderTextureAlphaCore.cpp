#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderTextureAlphaCore.h"
#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include <algorithm>

bool
coin_render_scene_texture_policy_supported(int32_t policy)
{
  return policy == SoSceneTexture2::NONE || policy == SoSceneTexture2::ALPHA_BLEND ||
         policy == SoSceneTexture2::ALPHA_TEST;
}

bool
coin_render_scene_texture_forces_transparency(int32_t policy)
{
  // Coin/GL sets FORCE_TRANSPARENCY_TRUE for both. useAlphaTest() is not
  // consumed by the renderer: do not infer an automatic discard threshold.
  return policy == SoSceneTexture2::ALPHA_BLEND || policy == SoSceneTexture2::ALPHA_TEST;
}

bool
coin_render_texture_has_transparency(const CoinRenderTextureImageSnapshot &image)
{
  if (image.sceneTransparencyFunction == SoSceneTexture2::NONE)
    return false;
  if (coin_render_scene_texture_forces_transparency(image.sceneTransparencyFunction))
    return true;
  if ((image.producerId || image.gpuToken) && !image.gpuOpaque)
    return true;
  if (image.producerId || image.gpuToken)
    return false;
  if (image.cubeFaceSize) {
    if (!CoinRenderTextureSamplingCore::validMipImage(image)) return true;
    uint64_t texels = 0;
    for (uint32_t extent = image.cubeFaceSize;; extent /= 2) {
      texels += 6 * uint64_t(extent) * extent;
      if (!image.cubeMipmapped || extent == 1) break;
    }
    for (uint64_t pixel=0; pixel<texels; ++pixel)
      if (image.pixelsRgba[size_t(pixel*4+3)] != 255) return true;
    return false;
  }
  if (image.volumeDepth) {
    if (!CoinRenderTextureSamplingCore::validMipImage(image)) return true;
    uint32_t base = 0;
    for (uint32_t w = image.volumeWidth, h = image.volumeHeight, d = image.volumeDepth;;) {
      for (uint32_t z=0; z<d; ++z)
        for (uint32_t y=0; y<h; ++y)
          for (uint32_t x=0; x<w; ++x) {
            const size_t ax=(z%image.volumeColumns)*w+x;
            const size_t ay=base+(z/image.volumeColumns)*h+y;
            if (image.pixelsRgba[(ay*image.width+ax)*4+3] != 255) return true;
          }
      base += h * ((d + image.volumeColumns - 1) / image.volumeColumns);
      if (!image.volumeMipmapped || (w == 1 && h == 1 && d == 1)) break;
      w = std::max(1u, w / 2); h = std::max(1u, h / 2); d = std::max(1u, d / 2);
    }
    return false;
  }
  if (image.format == CoinRenderTextureFormat::RGBA8_LINEAR ||
      image.format == CoinRenderTextureFormat::RGBA8_SRGB)
    return coin_render_image_has_transparency(image.pixelsRgba.data(), image.pixelsRgba.size() / 4, 4) ||
      coin_render_image_has_transparency(image.mipmapsRgba.data(), image.mipmapsRgba.size() / 4, 4);
  const size_t bytes =
      CoinRenderTextureFormatCore::levelBytes(image.width, image.height, image.format);
  if (!bytes || image.pixelsRgba.size() != bytes)
    return true;
  for (uint32_t y = 0; y < image.height; ++y)
    for (uint32_t x = 0; x < image.width; ++x)
      if (CoinRenderTextureFormatCore::texel(image.pixelsRgba, 0, image.width, image.format, x,
                                             y)[3] < 1.f)
        return true;
  return false;
}
