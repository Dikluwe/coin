#ifndef COIN_RENDER_TEXTURE_SAMPLING_CORE_H
#define COIN_RENDER_TEXTURE_SAMPLING_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include <algorithm>
#include <cmath>
// Captured quality selects sampler semantics; Core owns the deterministic
// RGBA8 box chain. GPU executors only upload the prepared bytes.
class CoinRenderTextureSamplingCore {
public:
  static bool powerOfTwo(uint32_t v) { return v && !(v & (v - 1)); }
  static size_t mipBytes(uint32_t w, uint32_t h) {
    size_t bytes = 0;
    while (w > 1 || h > 1) {
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      bytes += size_t(w) * h * 4;
    }
    return bytes;
  }
  static bool quality(float q, CoinRenderTextureFilter &filter) {
    if (!std::isfinite(q) || q < 0 || q > .85f)
      return false;
    filter = q < .2f   ? CoinRenderTextureFilter::NEAREST
             : q < .5f ? CoinRenderTextureFilter::LINEAR
             : q < .8f ? CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR
                       : CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
    return true;
  }
  static bool mipFilter(CoinRenderTextureFilter filter) {
    return static_cast<uint32_t>(filter) >= 2;
  }
  static bool validMipImage(const CoinRenderTextureImageSnapshot &image) {
    return !image.mipmapped
               ? image.mipmapsRgba.empty()
               : !image.producerId && !image.gpuToken && image.width <= 8192 &&
                     image.height <= 8192 && powerOfTwo(image.width) &&
                     powerOfTwo(image.height) &&
                     size_t(image.width) * image.height * 4 +
                             mipBytes(image.width, image.height) <=
                         128u * 1024u * 1024u &&
                     image.mipmapsRgba.size() ==
                         mipBytes(image.width, image.height);
  }
  static bool generate(CoinRenderTextureImageSnapshot &image) {
    if (image.width > 8192 || image.height > 8192 || !powerOfTwo(image.width) ||
        !powerOfTwo(image.height) || image.producerId || image.gpuToken ||
        image.pixelsRgba.size() != size_t(image.width) * image.height * 4 ||
        image.pixelsRgba.size() + mipBytes(image.width, image.height) >
            128u * 1024u * 1024u)
      return false;
    std::vector<uint8_t> result;
    uint32_t w = image.width, h = image.height;
    std::vector<uint8_t> previous = image.pixelsRgba;
    while (w > 1 || h > 1) {
      const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
      std::vector<uint8_t> next(size_t(nw) * nh * 4);
      const unsigned samples = (w > 1 ? 2 : 1) * (h > 1 ? 2 : 1);
      for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x)
          for (unsigned c = 0; c < 4; ++c) {
            unsigned sum = 0;
            for (unsigned dy = 0; dy < (h > 1 ? 2u : 1u); ++dy)
              for (unsigned dx = 0; dx < (w > 1 ? 2u : 1u); ++dx)
                sum +=
                    previous[((size_t(y * 2 + dy) * w) + (x * 2 + dx)) * 4 + c];
            next[(size_t(y) * nw + x) * 4 + c] =
                uint8_t((sum + (samples == 4 ? 2 : 0)) /
                        samples); // Coin box rounding in 2D; truncation in 1D
          }
      result.insert(result.end(), next.begin(), next.end());
      previous.swap(next);
      w = nw;
      h = nh;
    }
    image.mipmapsRgba.swap(result);
    image.mipmapped = true;
    return true;
  }
};
#endif
