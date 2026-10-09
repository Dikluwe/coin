#ifndef COIN_RENDER_TEXTURE_SAMPLING_CORE_H
#define COIN_RENDER_TEXTURE_SAMPLING_CORE_H
#include "rendering/coinrender/CoinRenderFramePlan.h"
#include "rendering/coinrender/CoinRenderTextureFormatCore.h"
#include <algorithm>
#include <cmath>
// Captured quality selects sampler semantics; Core owns the deterministic
// stored format-aware box chain. GPU executors upload the prepared bytes;
// direct RTT mipmaps are generated on the GPU after the producer render.
class CoinRenderTextureSamplingCore {
public:
  static bool powerOfTwo(uint32_t v) { return v && !(v & (v - 1)); }
  static uint32_t legacyPotExtent(uint32_t extent, bool scaleDown,
                                  bool useQuality, float quality) {
    if (!extent || extent > 8192) return 0;
    uint32_t result = 1;
    while (result < extent) result <<= 1;
    if (result > extent && result > 16 && scaleDown)
      result >>= 1;
    else if (result >= 256 && useQuality && quality < .7f &&
             result - extent > result / 8 && !scaleDown)
      result >>= 1;
    return result;
  }
  // CoinGL's fast_image_resize path, used for scale quality below 0.5.
  static bool legacyResizeNearest(CoinRenderTextureImageSnapshot &image,
                                  uint32_t newWidth, uint32_t newHeight) {
    if (!image.width || !image.height || !newWidth || !newHeight ||
        newWidth > 8192 || newHeight > 8192 || image.producerId ||
        image.gpuToken || image.components != 4 ||
        image.pixelsRgba.size() != size_t(image.width) * image.height * 4 ||
        size_t(newWidth) * newHeight * 4 > 128u * 1024u * 1024u)
      return false;
    if (newWidth == image.width && newHeight == image.height) return true;
    std::vector<uint8_t> resized(size_t(newWidth) * newHeight * 4);
    const float dx = float(image.width) / float(newWidth);
    const float dy = float(image.height) / float(newHeight);
    float sy = 0.0f;
    for (uint32_t y = 0; y < newHeight; ++y, sy += dy) {
      float sx = 0.0f;
      for (uint32_t x = 0; x < newWidth; ++x, sx += dx) {
        const size_t src = (size_t(std::min(uint32_t(sy), image.height - 1)) *
                            image.width + std::min(uint32_t(sx), image.width - 1)) * 4;
        std::copy_n(image.pixelsRgba.begin() + src, 4,
                    resized.begin() + (size_t(y) * newWidth + x) * 4);
      }
    }
    image.width = newWidth;
    image.height = newHeight;
    image.pixelsRgba.swap(resized);
    return true;
  }
  static size_t mipBytes(
      uint32_t w, uint32_t h,
      CoinRenderTextureFormat format = CoinRenderTextureFormat::RGBA8_LINEAR) {
    return CoinRenderTextureFormatCore::mipBytes(w, h, format);
  }
  static bool quality(float q, CoinRenderTextureFilter &filter) {
    if (!std::isfinite(q) || q < 0 || q > 1.f)
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
    using Format = CoinRenderTextureFormatCore;
    const size_t base =
        Format::levelBytes(image.width, image.height, image.format);
    if (!base || base + mipBytes(image.width, image.height, image.format) >
                     128u * 1024u * 1024u)
      return false;
    // GPU BC upload requires block-aligned base extents; lower mips are padded.
    if (Format::compressed(image.format) &&
        ((image.width % 4) || (image.height % 4)))
      return false;
    if (image.format == CoinRenderTextureFormat::RGBA16_FLOAT &&
        (!Format::finiteHalfPayload(image.pixelsRgba) ||
         !Format::finiteHalfPayload(image.mipmapsRgba)))
      return false;
    if (!image.mipmapped)
      return image.mipmapsRgba.empty();
    if (image.producerId || image.gpuToken)
      return image.pixelsRgba.empty() && image.mipmapsRgba.empty();
    return image.mipmapsRgba.size() ==
           mipBytes(image.width, image.height, image.format);
  }

  static bool compress(CoinRenderTextureImageSnapshot &image) {
    using Format = CoinRenderTextureFormatCore;
    if (image.format != CoinRenderTextureFormat::RGBA8_LINEAR &&
        image.format != CoinRenderTextureFormat::RGBA8_SRGB)
      return false;
    if (!image.width || !image.height || image.width % 4 || image.height % 4 ||
        image.producerId || image.gpuToken)
      return false;
    std::vector<uint8_t> base, mips;
    if (!Format::encodeBc3(image.pixelsRgba, image.width, image.height, base))
      return false;
    size_t offset = 0;
    uint32_t w = image.width, h = image.height;
    if (image.mipmapped)
      while (w > 1 || h > 1) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        const size_t bytes = size_t(w) * h * 4;
        if (bytes > image.mipmapsRgba.size() -
                        std::min(offset, image.mipmapsRgba.size()))
          return false;
        const std::vector<uint8_t> source(image.mipmapsRgba.begin() + offset,
                                          image.mipmapsRgba.begin() + offset +
                                              bytes);
        std::vector<uint8_t> encoded;
        if (!Format::encodeBc3(source, w, h, encoded))
          return false;
        mips.insert(mips.end(), encoded.begin(), encoded.end());
        offset += bytes;
      }
    if (offset != image.mipmapsRgba.size())
      return false;
    image.format = Format::srgb(image.format)
                       ? CoinRenderTextureFormat::BC3_SRGB
                       : CoinRenderTextureFormat::BC3_LINEAR;
    image.pixelsRgba.swap(base);
    image.mipmapsRgba.swap(mips);
    return true;
  }

  static bool generate(CoinRenderTextureImageSnapshot &image) {
    if (image.width > 8192 || image.height > 8192 || !image.width ||
        !image.height || !CoinRenderTextureFormatCore::valid(image.format) ||
        (image.format == CoinRenderTextureFormat::RGBA16_FLOAT &&
         !CoinRenderTextureFormatCore::finiteHalfPayload(image.pixelsRgba)) ||
        CoinRenderTextureFormatCore::compressed(image.format) ||
        image.producerId || image.gpuToken ||
        image.pixelsRgba.size() !=
            CoinRenderTextureFormatCore::levelBytes(image.width, image.height,
                                                    image.format) ||
        image.pixelsRgba.size() +
                mipBytes(image.width, image.height, image.format) >
            128u * 1024u * 1024u)
      return false;
    std::vector<uint8_t> result;
    uint32_t w = image.width, h = image.height;
    std::vector<uint8_t> previous = image.pixelsRgba;
    while (w > 1 || h > 1) {
      const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
      std::vector<uint8_t> next(
          CoinRenderTextureFormatCore::levelBytes(nw, nh, image.format));
      if (image.format != CoinRenderTextureFormat::RGBA8_LINEAR ||
          !powerOfTwo(w) || !powerOfTwo(h)) {
        // Exact area footprints include odd NPOT borders. Decode SRGB before
        // averaging and preserve float range for HDR; alpha is always linear.
        for (uint32_t y = 0; y < nh; ++y)
          for (uint32_t x = 0; x < nw; ++x) {
            double sums[4] = {};
            const uint32_t x0 = x * w, x1 = (x + 1) * w, y0 = y * h,
                           y1 = (y + 1) * h;
            for (uint32_t sy = y0 / nh; sy < (y1 + nh - 1) / nh; ++sy)
              for (uint32_t sx = x0 / nw; sx < (x1 + nw - 1) / nw; ++sx) {
                const uint32_t wx =
                    std::min(x1, (sx + 1) * nw) - std::max(x0, sx * nw);
                const uint32_t wy =
                    std::min(y1, (sy + 1) * nh) - std::max(y0, sy * nh);
                const auto color = CoinRenderTextureFormatCore::texel(
                    previous, 0, w, image.format, sx, sy);
                for (int c = 0; c < 4; ++c)
                  sums[c] += double(color[c]) * wx * wy;
              }
            SbVec4f color;
            for (int c = 0; c < 4; ++c)
              color[c] = float(sums[c] / (double(w) * h));
            CoinRenderTextureFormatCore::put(
                next,
                (size_t(y) * nw + x) *
                    (image.format == CoinRenderTextureFormat::RGBA16_FLOAT ? 8
                                                                           : 4),
                image.format, color);
          }
        result.insert(result.end(), next.begin(), next.end());
        previous.swap(next);
        w = nw;
        h = nh;
        continue;
      }
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
