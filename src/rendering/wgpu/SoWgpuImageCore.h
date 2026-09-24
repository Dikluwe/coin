#ifndef COIN_SOWGPUIMAGECORE_H
#define COIN_SOWGPUIMAGECORE_H

#include <Inventor/CoinWgpuExport.h>
#include <Inventor/SbVec2i32.h>

#include <cstdint>
#include <vector>

// Coin-native mechanical image transformations used at the renderer boundary.
// This type neither performs readback nor reports user-facing diagnostics.
class SoWgpuImageCore {
public:
  // Converts top-origin tightly packed RGBA8 rows to Coin's bottom-origin
  // texture convention. Invalid dimensions or byte counts leave pixels intact.
  COIN_WGPU_DLL_API static bool flipRgba8Rows(
    std::vector<uint8_t> & pixels, const SbVec2i32 & size);
};

#endif // !COIN_SOWGPUIMAGECORE_H
