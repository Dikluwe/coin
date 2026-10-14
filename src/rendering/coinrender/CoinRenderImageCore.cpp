#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#include "src/config.h"
#endif

#include "rendering/coinrender/CoinRenderImageCore.h"

#include <algorithm>
#include <cstddef>
#include <limits>

bool
CoinRenderImageCore::flipRgba8Rows(std::vector<uint8_t> & pixels,
                               const SbVec2i32 & size)
{
  if (size[0] <= 0 || size[1] <= 0) return false;

  const size_t width = static_cast<size_t>(size[0]);
  const size_t height = static_cast<size_t>(size[1]);
  if (width > std::numeric_limits<size_t>::max() / 4) return false;
  const size_t rowBytes = width * 4;
  if (height > std::numeric_limits<size_t>::max() / rowBytes ||
      pixels.size() != rowBytes * height) return false;

  for (size_t y = 0; y < height / 2; ++y) {
    std::swap_ranges(pixels.begin() + y * rowBytes,
                     pixels.begin() + (y + 1) * rowBytes,
                     pixels.begin() + (height - 1 - y) * rowBytes);
  }
  return true;
}

uint64_t CoinRenderImageCore::rgba8Digest(const std::vector<uint8_t>& pixels) {
  uint64_t hash = 14695981039346656037ULL;
  for (uint8_t value : pixels) {
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool CoinRenderImageCore::convertToRgba8(const uint8_t * source, size_t count,
                                        int components, std::vector<uint8_t> & output) {
  if (!source || !count || components < 1 || components > 4 ||
      count > std::numeric_limits<size_t>::max() / 4) return false;
  output.resize(count * 4);
  for (size_t i = 0; i < count; ++i) {
    const auto * pixel = source + i * components;
    output[i * 4] = pixel[0];
    output[i * 4 + 1] = components < 3 ? pixel[0] : pixel[1];
    output[i * 4 + 2] = components < 3 ? pixel[0] : pixel[2];
    output[i * 4 + 3] = components == 2 ? pixel[1] : components == 4 ? pixel[3] : 255;
  }
  return true;
}
