#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rendering/coinrender/CoinRenderFloatCore.h"
#include <cmath>
#include <cstring>
#include <limits>

bool
coin_render_is_finite(float value)
{
  if (sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559 &&
      std::numeric_limits<float>::radix == 2 && std::numeric_limits<float>::digits == 24) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
  }
  return std::isfinite(value);
}
