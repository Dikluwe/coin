#ifndef COIN_RENDER_FLOAT_CORE_H
#define COIN_RENDER_FLOAT_CORE_H

#include <Inventor/CoinRenderExport.h>

#include <cstdint>

// IEEE binary32 finiteness depends only on its exponent. memcpy preserves
// aliasing rules and also handles signed zero, subnormals and all NaN payloads.
// Keep the standard classification for other supported float representations.
COIN_RENDER_DLL_API bool coin_render_is_finite(float value);

#endif
