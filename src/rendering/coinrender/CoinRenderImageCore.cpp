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
