#ifndef COIN_RENDER_TEXTURE_SAMPLING_CORE_H
#define COIN_RENDER_TEXTURE_SAMPLING_CORE_H

#include <Inventor/CoinRenderExport.h>
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTextureFormatCore.h"
// Captured quality selects sampler semantics; Core owns the deterministic
// stored format-aware box chain. GPU executors upload the prepared bytes;
// direct RTT mipmaps are generated on the GPU after the producer render.
class CoinRenderTextureSamplingCore {
public:
  COIN_RENDER_DLL_API static bool powerOfTwo(uint32_t v);
  COIN_RENDER_DLL_API static uint32_t legacyPotExtent(uint32_t extent, bool scaleDown,
                                  bool useQuality, float quality);
  // CoinGL's fast_image_resize path, used for scale quality below 0.5.
  COIN_RENDER_DLL_API static bool legacyResizeNearest(CoinRenderTextureImageSnapshot &image,
                                  uint32_t newWidth, uint32_t newHeight);
  // GLU's gluScaleImage reconstruction with a one-pixel box footprint.
  // The source wraps at the edges, matching Mesa GLU's scale_internal.
  COIN_RENDER_DLL_API static bool legacyResizeGlu(CoinRenderTextureImageSnapshot &image,
                              uint32_t newWidth, uint32_t newHeight);
  COIN_RENDER_DLL_API static size_t mipBytes(
      uint32_t w, uint32_t h,
      CoinRenderTextureFormat format = CoinRenderTextureFormat::RGBA8_LINEAR);
  COIN_RENDER_DLL_API static bool quality(float q, CoinRenderTextureFilter &filter);
  COIN_RENDER_DLL_API static bool mipFilter(CoinRenderTextureFilter filter);
  COIN_RENDER_DLL_API static bool validMipImage(const CoinRenderTextureImageSnapshot &image);

  COIN_RENDER_DLL_API static bool compress(CoinRenderTextureImageSnapshot &image);

  COIN_RENDER_DLL_API static bool generate(CoinRenderTextureImageSnapshot &image);
};
#endif
