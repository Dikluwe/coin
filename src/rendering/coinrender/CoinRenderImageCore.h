#ifndef COIN_RENDER_IMAGE_CORE_H
#define COIN_RENDER_IMAGE_CORE_H

#include <Inventor/CoinRenderExport.h>
#include <Inventor/SbVec2i32.h>

#include <cstdint>
#include <vector>

// Coin-native mechanical image transformations used at the renderer boundary.
// This type neither performs readback nor reports user-facing diagnostics.
class CoinRenderImageCore {
public:
  // Converts top-origin tightly packed RGBA8 rows to Coin's bottom-origin
  // texture convention. Invalid dimensions or byte counts leave pixels intact.
  COIN_RENDER_DLL_API static bool flipRgba8Rows(
    std::vector<uint8_t> & pixels, const SbVec2i32 & size);
};

#endif // !COIN_RENDER_IMAGE_CORE_H
