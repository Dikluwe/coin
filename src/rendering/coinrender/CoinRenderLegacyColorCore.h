#ifndef COIN_RENDER_LEGACY_COLOR_CORE_H
#define COIN_RENDER_LEGACY_COLOR_CORE_H

#include <Inventor/rendering/CoinRenderTarget.h>
#include <vector>
#include <cstdint>

// Pure conversion of top-down RGBA8 to CoinGL's bottom-up getBuffer layout.
// On failure, output is empty. The target checks publication state first.
COIN_RENDER_DLL_API bool coin_render_convert_legacy_components(
  const uint8_t * rgba, uint32_t width, uint32_t height,
  CoinRenderLegacyComponents components, std::vector<uint8_t> & output);

COIN_RENDER_DLL_API bool coin_render_encode_legacy_rgb(
  const uint8_t * bottomUpPixels, uint32_t width, uint32_t height,
  CoinRenderLegacyComponents components, std::vector<uint8_t> & output);

COIN_RENDER_DLL_API bool coin_render_encode_legacy_postscript(
  const uint8_t * bottomUpPixels, uint32_t width, uint32_t height,
  CoinRenderLegacyComponents components, const SbVec2f & printSize,
  float dpi, std::vector<uint8_t> & output);

#endif
