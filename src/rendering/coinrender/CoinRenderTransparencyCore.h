#ifndef COIN_RENDER_TRANSPARENCY_CORE_H
#define COIN_RENDER_TRANSPARENCY_CORE_H

#include <Inventor/rendering/CoinRenderCapabilities.h>
#include <cstdint>
#include <limits>
#include <string>

static const uint32_t COIN_RENDER_MAX_PEEL_LAYERS = 8;

struct CoinRenderTransparencyOptions {
  CoinRenderTransparencyMode mode = COIN_RENDER_TRANSPARENCY_COIN;
  uint32_t layers = 4;
  uint64_t bufferBudget = uint64_t(256) * 1024 * 1024;
};

inline bool coin_render_same_transparency_options(const CoinRenderTransparencyOptions& a,
                                                  const CoinRenderTransparencyOptions& b) {
  return a.mode == b.mode && a.layers == b.layers && a.bufferBudget == b.bufferBudget;
}

// Conservative attachment budget, including depth and alignment overhead.
// Core owns the limit; Infra allocates its concrete formats below this bound.
inline bool coin_render_transparency_budget(uint32_t width, uint32_t height,
                                            const CoinRenderTransparencyOptions& options,
                                            bool allocateLayers, uint64_t& required,
                                            std::string& diagnostic) {
  required = 0;
  if (options.mode < COIN_RENDER_TRANSPARENCY_COIN ||
      options.mode > COIN_RENDER_TRANSPARENCY_WEIGHTED_OIT) {
    diagnostic = "Invalid transparency option";
    return false;
  }
  if (options.layers < 1 || options.layers > COIN_RENDER_MAX_PEEL_LAYERS ||
      options.bufferBudget == 0) {
    diagnostic = "Transparency requires 1..8 layers and a nonzero buffer budget";
    return false;
  }
  if (!allocateLayers)
    return true;
  const uint64_t pixels = uint64_t(width) * height;
  const uint64_t bytesPerPixel = uint64_t(options.layers) * 16 + 8;
  if (!width || !height || pixels > std::numeric_limits<uint64_t>::max() / bytesPerPixel) {
    diagnostic = "Transparency attachment dimensions overflow the budget calculation";
    return false;
  }
  required = pixels * bytesPerPixel;
  if (required > options.bufferBudget) {
    diagnostic = "Transparency attachment budget exceeded: required=" + std::to_string(required) +
                 " available=" + std::to_string(options.bufferBudget);
    return false;
  }
  return true;
}

#endif
