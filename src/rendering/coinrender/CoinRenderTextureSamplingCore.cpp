#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderTextureSamplingCore.h"
#include "rendering/coinrender/CoinRenderCubeTextureCore.h"
#include <algorithm>
#include <cmath>

bool
CoinRenderTextureSamplingCore::powerOfTwo(uint32_t v)
{
  return v && !(v & (v - 1));
}

uint32_t
CoinRenderTextureSamplingCore::legacyPotExtent(uint32_t extent, bool scaleDown, bool useQuality,
                                               float quality)
{
  if (!extent || extent > 8192)
    return 0;
  uint32_t result = 1;
  while (result < extent)
    result <<= 1;
  if (result > extent && result > 16 && scaleDown)
    result >>= 1;
  else if (result >= 256 && useQuality && quality < .7f && result - extent > result / 8 &&
           !scaleDown)
    result >>= 1;
  return result;
}

bool
CoinRenderTextureSamplingCore::legacyResizeNearest(CoinRenderTextureImageSnapshot &image,
                                                   uint32_t newWidth, uint32_t newHeight)
{
  if (!image.width || !image.height || image.width > 8192 || image.height > 8192 || !newWidth ||
      !newHeight || newWidth > 8192 || newHeight > 8192 || image.producerId || image.gpuToken ||
      image.components != 4 || image.pixelsRgba.size() != size_t(image.width) * image.height * 4 ||
      size_t(newWidth) * newHeight * 4 > 128u * 1024u * 1024u)
    return false;
  if (newWidth == image.width && newHeight == image.height)
    return true;
  std::vector<uint8_t> resized(size_t(newWidth) * newHeight * 4);
  const float dx = float(image.width) / float(newWidth);
  const float dy = float(image.height) / float(newHeight);
  float sy = 0.0f;
  for (uint32_t y = 0; y < newHeight; ++y, sy += dy) {
    float sx = 0.0f;
    for (uint32_t x = 0; x < newWidth; ++x, sx += dx) {
      const size_t src = (size_t(std::min(uint32_t(sy), image.height - 1)) * image.width +
                          std::min(uint32_t(sx), image.width - 1)) *
                         4;
      std::copy_n(image.pixelsRgba.begin() + src, 4,
                  resized.begin() + (size_t(y) * newWidth + x) * 4);
    }
  }
  image.width = newWidth;
  image.height = newHeight;
  image.pixelsRgba.swap(resized);
  return true;
}

bool
CoinRenderTextureSamplingCore::legacyResizeGlu(CoinRenderTextureImageSnapshot &image,
                                               uint32_t newWidth, uint32_t newHeight)
{
  if (!image.width || !image.height || image.width > 8192 || image.height > 8192 || !newWidth ||
      !newHeight || newWidth > 8192 || newHeight > 8192 || image.producerId || image.gpuToken ||
      image.components != 4 || image.pixelsRgba.size() != size_t(image.width) * image.height * 4 ||
      size_t(newWidth) * newHeight * 4 > 128u * 1024u * 1024u)
    return false;
  if (newWidth == image.width && newHeight == image.height)
    return true;
  std::vector<uint8_t> resized(size_t(newWidth) * newHeight * 4);
  const float dx = float(image.width) / float(newWidth);
  const float dy = float(image.height) / float(newHeight);
  for (uint32_t y = 0; y < newHeight; ++y) {
    const float centerY = dy * (float(y) + .5f);
    const float halfY = std::max(dy, 1.0f) * .5f;
    const float lowY = centerY - halfY, highY = centerY + halfY;
    for (uint32_t x = 0; x < newWidth; ++x) {
      const float centerX = dx * (float(x) + .5f);
      const float halfX = std::max(dx, 1.0f) * .5f;
      const float lowX = centerX - halfX, highX = centerX + halfX;
      float sum[4] = {0, 0, 0, 0}, area = 0;
      for (int sy = int(std::floor(lowY)); sy < int(std::ceil(highY)); ++sy) {
        const float wy = std::min(highY, float(sy + 1)) - std::max(lowY, float(sy));
        if (wy <= 0)
          continue;
        const uint32_t iy = uint32_t((sy + int(image.height)) % int(image.height));
        for (int sx = int(std::floor(lowX)); sx < int(std::ceil(highX)); ++sx) {
          const float wx = std::min(highX, float(sx + 1)) - std::max(lowX, float(sx));
          if (wx <= 0)
            continue;
          const float weight = wx * wy;
          const size_t src =
              (size_t(iy) * image.width + uint32_t((sx + int(image.width)) % int(image.width))) * 4;
          for (int c = 0; c < 4; ++c)
            sum[c] += weight * image.pixelsRgba[src + c];
          area += weight;
        }
      }
      const size_t dst = (size_t(y) * newWidth + x) * 4;
      for (int c = 0; c < 4; ++c)
        resized[dst + c] = uint8_t(std::min(255.0f, (sum[c] + .5f) / area));
    }
  }
  image.width = newWidth;
  image.height = newHeight;
  image.pixelsRgba.swap(resized);
  return true;
}

size_t
CoinRenderTextureSamplingCore::mipBytes(uint32_t w, uint32_t h, CoinRenderTextureFormat format)
{
  return CoinRenderTextureFormatCore::mipBytes(w, h, format);
}

bool
CoinRenderTextureSamplingCore::quality(float q, CoinRenderTextureFilter &filter)
{
  if (!std::isfinite(q) || q < 0 || q > 1.f)
    return false;
  filter = q < .2f   ? CoinRenderTextureFilter::NEAREST
           : q < .5f ? CoinRenderTextureFilter::LINEAR
           : q < .8f ? CoinRenderTextureFilter::NEAREST_MIPMAP_LINEAR
                     : CoinRenderTextureFilter::LINEAR_MIPMAP_LINEAR;
  return true;
}

bool
CoinRenderTextureSamplingCore::mipFilter(CoinRenderTextureFilter filter)
{
  return static_cast<uint32_t>(filter) >= 2;
}

bool
CoinRenderTextureSamplingCore::validMipImage(const CoinRenderTextureImageSnapshot &image)
{
  using Format = CoinRenderTextureFormatCore;
  if (image.cubeFaceSize) return CoinRenderCubeTextureCore::valid(image);
  if (image.cubeSourceComponents || image.cubeMipmapped) return false;
  if (image.volumeDepth != 0) {
    if (image.format != CoinRenderTextureFormat::RGBA8_LINEAR ||
        image.producerId || image.gpuToken || image.mipmapped ||
        !image.mipmapsRgba.empty() || image.components != 4 ||
        !image.volumeWidth || !image.volumeHeight ||
        !image.volumeDepth || !powerOfTwo(image.volumeColumns) ||
        image.volumeWidth > 256 || image.volumeHeight > 256 ||
        image.volumeDepth > 256 || image.volumeSourceComponents < 1 ||
        image.volumeSourceComponents > 4)
      return false;
    uint32_t cols = 1;
    while (cols * cols < image.volumeDepth) cols <<= 1;
    if (cols != image.volumeColumns || image.width != cols * image.volumeWidth)
      return false;
    uint32_t rows = 0;
    for (uint32_t w = image.volumeWidth, h = image.volumeHeight,
                  d = image.volumeDepth;;) {
      rows += h * ((d + cols - 1) / cols);
      if (!image.volumeMipmapped || (w == 1 && h == 1 && d == 1)) break;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      d = std::max(1u, d / 2);
    }
    if (image.height != rows || image.width > 8192 || rows > 8192 ||
        uint64_t(image.width) * rows * 4 > 128ull * 1024ull * 1024ull ||
        image.pixelsRgba.size() != uint64_t(image.width) * rows * 4)
      return false;
    uint32_t base = 0;
    for (uint32_t w = image.volumeWidth, h = image.volumeHeight,
                  d = image.volumeDepth;;) {
      const uint32_t levelRows = (d + cols - 1) / cols;
      for (uint32_t row = 0; row < levelRows; ++row) {
        const uint32_t used = std::min(cols, d - row * cols) * w;
        for (uint32_t y = 0; y < h; ++y) {
          const size_t start = (size_t(base + row * h + y) * image.width + used) * 4;
          const size_t end = size_t(base + row * h + y + 1) * image.width * 4;
          if (std::any_of(image.pixelsRgba.begin() + start, image.pixelsRgba.begin() + end,
                          [](uint8_t value) { return value != 0; }))
            return false;
        }
      }
      base += h * levelRows;
      if (!image.volumeMipmapped || (w == 1 && h == 1 && d == 1)) break;
      w = std::max(1u, w / 2); h = std::max(1u, h / 2); d = std::max(1u, d / 2);
    }
    return true;
  }
  if (image.volumeWidth || image.volumeHeight || image.volumeColumns ||
      image.volumeSourceComponents || image.volumeMipmapped)
    return false;
  const size_t base = Format::levelBytes(image.width, image.height, image.format);
  if (!base || base + mipBytes(image.width, image.height, image.format) > 128u * 1024u * 1024u)
    return false;
  // GPU BC upload requires block-aligned base extents; lower mips are padded.
  if (Format::compressed(image.format) && ((image.width % 4) || (image.height % 4)))
    return false;
  if (image.format == CoinRenderTextureFormat::RGBA16_FLOAT &&
      (!Format::finiteHalfPayload(image.pixelsRgba) ||
       !Format::finiteHalfPayload(image.mipmapsRgba)))
    return false;
  if (!image.mipmapped)
    return image.mipmapsRgba.empty();
  if (image.producerId || image.gpuToken)
    return image.pixelsRgba.empty() && image.mipmapsRgba.empty();
  return image.mipmapsRgba.size() == mipBytes(image.width, image.height, image.format);
}

bool
CoinRenderTextureSamplingCore::compress(CoinRenderTextureImageSnapshot &image)
{
  using Format = CoinRenderTextureFormatCore;
  if (image.format != CoinRenderTextureFormat::RGBA8_LINEAR &&
      image.format != CoinRenderTextureFormat::RGBA8_SRGB)
    return false;
  if (!image.width || !image.height || image.width % 4 || image.height % 4 || image.producerId ||
      image.gpuToken)
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
      if (bytes > image.mipmapsRgba.size() - std::min(offset, image.mipmapsRgba.size()))
        return false;
      const std::vector<uint8_t> source(image.mipmapsRgba.begin() + offset,
                                        image.mipmapsRgba.begin() + offset + bytes);
      std::vector<uint8_t> encoded;
      if (!Format::encodeBc3(source, w, h, encoded))
        return false;
      mips.insert(mips.end(), encoded.begin(), encoded.end());
      offset += bytes;
    }
  if (offset != image.mipmapsRgba.size())
    return false;
  image.format = Format::srgb(image.format) ? CoinRenderTextureFormat::BC3_SRGB
                                            : CoinRenderTextureFormat::BC3_LINEAR;
  image.pixelsRgba.swap(base);
  image.mipmapsRgba.swap(mips);
  return true;
}

bool
CoinRenderTextureSamplingCore::generate(CoinRenderTextureImageSnapshot &image)
{
  if (image.width > 8192 || image.height > 8192 || !image.width || !image.height ||
      !CoinRenderTextureFormatCore::valid(image.format) ||
      (image.format == CoinRenderTextureFormat::RGBA16_FLOAT &&
       !CoinRenderTextureFormatCore::finiteHalfPayload(image.pixelsRgba)) ||
      CoinRenderTextureFormatCore::compressed(image.format) || image.producerId || image.gpuToken ||
      image.pixelsRgba.size() !=
          CoinRenderTextureFormatCore::levelBytes(image.width, image.height, image.format) ||
      image.pixelsRgba.size() + mipBytes(image.width, image.height, image.format) >
          128u * 1024u * 1024u)
    return false;
  std::vector<uint8_t> result;
  uint32_t w = image.width, h = image.height;
  std::vector<uint8_t> previous = image.pixelsRgba;
  while (w > 1 || h > 1) {
    const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    std::vector<uint8_t> next(CoinRenderTextureFormatCore::levelBytes(nw, nh, image.format));
    if (image.format != CoinRenderTextureFormat::RGBA8_LINEAR || !powerOfTwo(w) || !powerOfTwo(h)) {
      // Exact area footprints include odd NPOT borders. Decode SRGB before
      // averaging and preserve float range for HDR; alpha is always linear.
      for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x) {
          double sums[4] = {};
          const uint32_t x0 = x * w, x1 = (x + 1) * w, y0 = y * h, y1 = (y + 1) * h;
          for (uint32_t sy = y0 / nh; sy < (y1 + nh - 1) / nh; ++sy)
            for (uint32_t sx = x0 / nw; sx < (x1 + nw - 1) / nw; ++sx) {
              const uint32_t wx = std::min(x1, (sx + 1) * nw) - std::max(x0, sx * nw);
              const uint32_t wy = std::min(y1, (sy + 1) * nh) - std::max(y0, sy * nh);
              const auto color =
                  CoinRenderTextureFormatCore::texel(previous, 0, w, image.format, sx, sy);
              for (int c = 0; c < 4; ++c)
                sums[c] += double(color[c]) * wx * wy;
            }
          SbVec4f color;
          for (int c = 0; c < 4; ++c)
            color[c] = float(sums[c] / (double(w) * h));
          CoinRenderTextureFormatCore::put(
              next,
              (size_t(y) * nw + x) *
                  (image.format == CoinRenderTextureFormat::RGBA16_FLOAT ? 8 : 4),
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
              sum += previous[((size_t(y * 2 + dy) * w) + (x * 2 + dx)) * 4 + c];
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

bool
CoinRenderTextureSamplingCore::packVolumeAtlas(const std::vector<uint8_t> &rgba,
    uint32_t width, uint32_t height, uint32_t depth, uint32_t sourceComponents,
    bool withMips, CoinRenderTextureImageSnapshot &image)
{
  constexpr uint64_t budget = 128ull * 1024ull * 1024ull;
  if (!width || !height || !depth ||
      width > 256 || height > 256 || depth > 256 ||
      sourceComponents < 1 || sourceComponents > 4 ||
      uint64_t(width) * height * depth * 4 != rgba.size())
    return false;

  uint32_t columns = 1;
  while (columns * columns < depth) columns <<= 1;
  const uint32_t atlasWidth = width * columns;
  uint32_t atlasHeight = 0;
  for (uint32_t w = width, h = height, d = depth;;) {
    const uint32_t rows = (d + columns - 1) / columns;
    if (h > (8192u - atlasHeight) / rows) return false;
    atlasHeight += h * rows;
    if (!withMips || (w == 1 && h == 1 && d == 1)) break;
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    d = std::max(1u, d / 2);
  }
  const uint64_t atlasBytes = uint64_t(atlasWidth) * atlasHeight * 4;
  if (atlasWidth > 8192 || atlasHeight > 8192 || atlasBytes > budget)
    return false;

  std::vector<uint8_t> packed(size_t(atlasBytes), 0);
  const std::vector<uint8_t> *level = &rgba;
  std::vector<uint8_t> generated;
  uint32_t yBase = 0;
  for (uint32_t w = width, h = height, d = depth;;) {
    for (uint32_t z = 0; z < d; ++z) {
      const uint32_t xBase = (z % columns) * w;
      const uint32_t sliceY = yBase + (z / columns) * h;
      for (uint32_t y = 0; y < h; ++y) {
        const size_t source = ((size_t(z) * h + y) * w) * 4;
        const size_t dest = ((size_t(sliceY + y) * atlasWidth) + xBase) * 4;
        std::copy_n(level->data() + source, size_t(w) * 4, packed.data() + dest);
      }
    }
    yBase += h * ((d + columns - 1) / columns);
    if (!withMips || (w == 1 && h == 1 && d == 1)) break;
    const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2),
                   nd = std::max(1u, d / 2);
    std::vector<uint8_t> next(size_t(nw) * nh * nd * 4);
    if ((w == 1 || w % 2 == 0) && (h == 1 || h % 2 == 0) &&
        (d == 1 || d % 2 == 0)) {
      // Preserve the established POT box path, also exact for even NPOT axes.
      const uint32_t count = (w > 1 ? 2u : 1u) * (h > 1 ? 2u : 1u) *
                             (d > 1 ? 2u : 1u);
      for (uint32_t z = 0; z < nd; ++z)
        for (uint32_t y = 0; y < nh; ++y)
          for (uint32_t x = 0; x < nw; ++x)
            for (uint32_t c = 0; c < 4; ++c) {
              uint32_t sum = 0;
              for (uint32_t dz = 0; dz < (d > 1 ? 2u : 1u); ++dz)
                for (uint32_t dy = 0; dy < (h > 1 ? 2u : 1u); ++dy)
                  for (uint32_t dx = 0; dx < (w > 1 ? 2u : 1u); ++dx)
                    sum += (*level)[(((size_t(z * 2 + dz) * h + y * 2 + dy) * w +
                                       x * 2 + dx) * 4) + c];
              next[((size_t(z) * nh + y) * nw + x) * 4 + c] =
                  uint8_t((sum + count / 2) / count);
            }
    } else {
      // Integer overlap numerators make each box integral exact, including
      // odd borders. Their common denominator cancels on normalization; use
      // 64 bits for the weighted channel sum and round only once per level.
      const uint64_t area = uint64_t(w) * h * d;
      for (uint32_t z = 0; z < nd; ++z)
        for (uint32_t y = 0; y < nh; ++y)
          for (uint32_t x = 0; x < nw; ++x) {
            const uint32_t x0 = x * w, x1 = (x + 1) * w;
            const uint32_t y0 = y * h, y1 = (y + 1) * h;
            const uint32_t z0 = z * d, z1 = (z + 1) * d;
            uint64_t sums[4] = {};
            for (uint32_t sz = z0 / nd; sz < (z1 + nd - 1) / nd; ++sz) {
              const uint32_t wz = std::min(z1, (sz + 1) * nd) - std::max(z0, sz * nd);
              for (uint32_t sy = y0 / nh; sy < (y1 + nh - 1) / nh; ++sy) {
                const uint32_t wy = std::min(y1, (sy + 1) * nh) - std::max(y0, sy * nh);
                for (uint32_t sx = x0 / nw; sx < (x1 + nw - 1) / nw; ++sx) {
                  const uint32_t wx = std::min(x1, (sx + 1) * nw) - std::max(x0, sx * nw);
                  const uint64_t weight = uint64_t(wx) * wy * wz;
                  const size_t source = ((size_t(sz) * h + sy) * w + sx) * 4;
                  for (uint32_t c = 0; c < 4; ++c)
                    sums[c] += (*level)[source + c] * weight;
                }
              }
            }
            for (uint32_t c = 0; c < 4; ++c)
              next[((size_t(z) * nh + y) * nw + x) * 4 + c] =
                  uint8_t((sums[c] + area / 2) / area);
          }
    }
    generated.swap(next);
    level = &generated;
    w = nw; h = nh; d = nd;
  }
  image.format = CoinRenderTextureFormat::RGBA8_LINEAR;
  image.width = atlasWidth;
  image.height = atlasHeight;
  image.volumeWidth = width;
  image.volumeHeight = height;
  image.volumeDepth = depth;
  image.volumeColumns = columns;
  image.volumeSourceComponents = sourceComponents;
  image.volumeMipmapped = withMips;
  image.mipmapped = false; // The atlas has an explicit spatial chain, not 2D hardware mips.
  image.mipmapsRgba.clear();
  image.pixelsRgba.swap(packed);
  return true;
}
