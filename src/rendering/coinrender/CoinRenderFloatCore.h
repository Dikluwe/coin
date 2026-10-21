#ifndef COIN_RENDER_FLOAT_CORE_H
#define COIN_RENDER_FLOAT_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

// IEEE binary32 finiteness depends only on its exponent. memcpy preserves
// aliasing rules and also handles signed zero, subnormals and all NaN payloads.
// Keep the standard classification for other supported float representations.
COIN_RENDER_DLL_API bool coin_render_is_finite(float value);

// Hot validation loops use the same classifier in their translation unit so
// that checking every captured vertex does not require an external call for
// each float. The exported function remains the ABI entry point.
static inline bool coin_render_is_finite_inline(float value)
{
  if (sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559 &&
      std::numeric_limits<float>::radix == 2 && std::numeric_limits<float>::digits == 24) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
  }
  return std::isfinite(value);
}

#endif
